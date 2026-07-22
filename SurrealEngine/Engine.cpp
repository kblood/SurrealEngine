
#include "Precomp.h"
#include "Engine.h"
#include "Utils/File.h"
#include "Utils/StrTools.h"
#include "Utils/CommandLine.h"
#include "Utils/SHA1Sum.h"
#include "Render/RenderSubsystem.h"
#include "Package/PackageManager.h"
#include "Package/ObjectStream.h"
#include "UObject/ULevel.h"
#include "UObject/UFont.h"
#include "UObject/UMesh.h"
#include "UObject/UActor.h"
#include "UObject/ObjectTravelInfo.h"
#include "UObject/UTexture.h"
#include "UObject/UMusic.h"
#include "UObject/USound.h"
#include "UObject/UClass.h"
#include "UObject/UClient.h"
#include "UObject/USubsystem.h"
#include "UObject/UFlag.h"
#include "UObject/UConSys.h"
#include "Math/quaternion.h"
#include "Math/FrustumPlanes.h"
#include "GameWindow.h"
#include "RenderDevice/RenderDevice.h"
#include "VM/Frame.h"
#include "VM/ScriptCall.h"
#include "Video/VideoPlayer.h"
#include <chrono>
#include <limits>
#include <set>

#ifdef __EMSCRIPTEN__
#include "WebXR/WebXRFrameBridge.h"
#include "WebXR/WebXRInputState.h"
#include "WebXR/WebXRHaptics.h"
#include "Package/Package.h"
#endif

namespace
{
	// UE1's input scripts expect movement-sized axis values, not normalized
	// gamepad values. A held keyboard movement alias conventionally produces
	// Speed=350 * press-delta=20 = 7000. The stock JoyX/JoyY convention uses
	// Speed=2, so WebXR supplies 3500 axis units at full stick deflection.
	constexpr float WebXRUE1MovementScale = 7000.0f;
	constexpr float WebXRJoystickAxisScale = WebXRUE1MovementScale / 2.0f;
	constexpr float WebXRYawUnitsPerDegree = 65536.0f / 360.0f;
	constexpr float WebXRSmoothTurnDeadZone = 0.15f;
	constexpr uint32_t WebXRActionButtonCount = 12;

	struct WebXRMovementAxes
	{
		float Strafe = 0.0f;
		float Forward = 0.0f;
	};

	struct WebXRActionEdges
	{
		bool RecenterPressed = false;
		bool MenuPressed = false;
		bool MenuReleased = false;
	};

	bool TryWebXRHorizontalForward(const vec3& source, vec3& result)
	{
		if (!std::isfinite(source.x) || !std::isfinite(source.y))
			return false;
		const float lengthSquared = source.x * source.x + source.y * source.y;
		if (!std::isfinite(lengthSquared) || lengthSquared < 0.000001f)
			return false;
		const float inverseLength = 1.0f / std::sqrt(lengthSquared);
		result = vec3(source.x * inverseLength, source.y * inverseLength, 0.0f);
		return true;
	}

	vec3 ResolveWebXRMovementForward(Engine::WebXRMovementReference requested,
		bool headTracked, const vec3& headForward, bool handTracked, const vec3& handForward,
		Engine::WebXRMovementReference& used, bool& fellBack)
	{
		used = Engine::WebXRMovementReference::Body;
		fellBack = false;
		if (requested == Engine::WebXRMovementReference::Body)
			return vec3(1.0f, 0.0f, 0.0f);

		vec3 horizontalForward;
		const bool resolved = requested == Engine::WebXRMovementReference::Head ?
			(headTracked && TryWebXRHorizontalForward(headForward, horizontalForward)) :
			(handTracked && TryWebXRHorizontalForward(handForward, horizontalForward));
		if (!resolved)
		{
			fellBack = true;
			return vec3(1.0f, 0.0f, 0.0f);
		}
		used = requested;
		return horizontalForward;
	}

	WebXRMovementAxes TransformWebXRMovementAxes(float strafe, float forward,
		const vec3& referenceForward)
	{
		// Work entirely in the body-local horizontal plane. This changes only the
		// movement axes supplied to UE1; tracking never enters pawn transforms.
		const vec3 referenceRight(-referenceForward.y, referenceForward.x, 0.0f);
		const vec3 movement = referenceForward * forward + referenceRight * strafe;
		return { movement.y, movement.x };
	}

	uint32_t WebXRActionButtonMask(uint32_t button)
	{
		return button >= 1 && button <= WebXRActionButtonCount ? 1u << (button - 1u) : 0u;
	}

	WebXRActionEdges ComputeWebXRActionEdges(uint32_t previousButtons, uint32_t currentButtons,
		uint32_t recenterButton, uint32_t menuButton)
	{
		const uint32_t pressed = ~previousButtons & currentButtons;
		const uint32_t released = previousButtons & ~currentButtons;
		const uint32_t recenterMask = WebXRActionButtonMask(recenterButton);
		const uint32_t menuMask = WebXRActionButtonMask(menuButton);
		return {
			recenterMask != 0 && (pressed & recenterMask) != 0,
			menuMask != 0 && (pressed & menuMask) != 0,
			menuMask != 0 && (released & menuMask) != 0
		};
	}

	const char* WebXRActionButtonName(uint32_t button)
	{
		static constexpr const char* Names[WebXRActionButtonCount + 1] = {
			"None", "LeftTrigger", "LeftSqueeze", "LeftTouchpad", "LeftStick",
			"LeftPrimary", "LeftSecondary", "RightTrigger", "RightSqueeze",
			"RightTouchpad", "RightStick", "RightPrimary", "RightSecondary"
		};
		return button <= WebXRActionButtonCount ? Names[button] : Names[0];
	}

	uint32_t ParseWebXRActionButton(std::string value, uint32_t fallback)
	{
		for (char& c : value)
			c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		if (value == "none" || value == "disabled" || value == "off" || value == "0")
			return 0;
		for (uint32_t button = 1; button <= WebXRActionButtonCount; button++)
		{
			std::string name = WebXRActionButtonName(button);
			for (char& c : name)
				c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
			if (value == name || value == std::to_string(button))
				return button;
		}
		return fallback;
	}

	bool IsWebXRActionButtonUnbound(const std::map<std::string, std::string>& bindings,
		uint32_t button)
	{
		if (button == 0)
			return false;
		const std::string key = "Joy" + std::to_string(button);
		const auto it = bindings.find(key);
		return it == bindings.end() || it->second.empty();
	}

#ifdef __EMSCRIPTEN__
	enum class WebXRWeaponAimScopeKind
	{
		None,
		Ballistic,
		TargetAcquisition,
		Presentation
	};

	struct WebXRWeaponCallMetadata
	{
		std::string PackageName;
		std::string ClassName;
		std::string DeclaringStateName;
		std::string ActiveStateName;
		std::string FunctionName;
	};

	bool WebXRNameEquals(const std::string& value, const char* expected)
	{
		return StrTools::equals_ignore_case(value, expected);
	}

	WebXRWeaponAimScopeKind ClassifyWebXRWeaponCall(const WebXRWeaponCallMetadata& call)
	{
		// Guided-warhead steering is a separate camera/control policy. Never
		// widen this direction-only weapon hook to cover it implicitly.
		if (WebXRNameEquals(call.ClassName, "GuidedWarShell"))
			return WebXRWeaponAimScopeKind::None;

		if (WebXRNameEquals(call.FunctionName, "TraceFire") ||
			WebXRNameEquals(call.FunctionName, "ProjectileFire"))
			return WebXRWeaponAimScopeKind::Ballistic;

		// Instance ownership is checked by EnterWebXRWeaponAimScope. Accept the
		// exact global presentation function independent of its declaring package
		// so Engine.Weapon and mod overrides/inheritance remain usable.
		const bool globalFunction = call.DeclaringStateName.empty();
		if (globalFunction && WebXRNameEquals(call.FunctionName, "RenderOverlays"))
			return WebXRWeaponAimScopeKind::Presentation;

		if (!WebXRNameEquals(call.PackageName, "Botpack"))
			return WebXRWeaponAimScopeKind::None;

		auto global = [&](const char* className, const char* functionName)
		{
			return globalFunction && WebXRNameEquals(call.ClassName, className) &&
				WebXRNameEquals(call.FunctionName, functionName);
		};
		auto state = [&](const char* className, const char* stateName, const char* functionName)
		{
			return WebXRNameEquals(call.ClassName, className) &&
				WebXRNameEquals(call.DeclaringStateName, stateName) &&
				WebXRNameEquals(call.ActiveStateName, stateName) &&
				WebXRNameEquals(call.FunctionName, functionName);
		};

		if (global("UT_FlakCannon", "Fire") || global("UT_FlakCannon", "AltFire") ||
			state("UT_Eightball", "FireRockets", "BeginState") ||
			global("Translocator", "ThrowTarget") || global("ChainSaw", "Slash") ||
			global("ImpactHammer", "TraceAltFire") || state("ImpactHammer", "Firing", "Tick"))
			return WebXRWeaponAimScopeKind::Ballistic;

		if (global("UT_Eightball", "CheckTarget"))
			return WebXRWeaponAimScopeKind::TargetAcquisition;

		return WebXRWeaponAimScopeKind::None;
	}

	WebXRWeaponCallMetadata GetWebXRWeaponCallMetadata(UFunction* func, UObject* instance)
	{
		WebXRWeaponCallMetadata result;
		if (!func)
			return result;

		result.FunctionName = func->Name.ToString();
		result.ActiveStateName = instance ? instance->GetStateName().ToString() : std::string();
		UClass* declaringClass = nullptr;
		for (UStruct* owner = func->StructParent; owner; owner = owner->StructParent)
		{
			// UClass derives from UState. Test UClass first or every global
			// function will be mislabelled as a state function.
			if (UClass* cls = UObject::TryCast<UClass>(owner))
			{
				declaringClass = cls;
				result.ClassName = cls->Name.ToString();
				break;
			}
			if (result.DeclaringStateName.empty())
			{
				if (UState* functionState = UObject::TryCast<UState>(owner))
					result.DeclaringStateName = functionState->Name.ToString();
			}
		}
		if (declaringClass && declaringClass->package)
			result.PackageName = declaringClass->package->GetPackageName().ToString();
		return result;
	}

	std::function<void()> BeginWebXRRotationOverride(Rotator& target, const Rotator& replacement)
	{
		Rotator* targetPtr = &target;
		const Rotator saved = target;
		target = replacement;
		return [targetPtr, saved]() { *targetPtr = saved; };
	}

	std::function<void()> BeginWebXRPresentationRotationOverride(Rotator& pawnRotation,
		Rotator& weaponRotation, const Rotator& replacement)
	{
		Rotator* pawnPtr = &pawnRotation;
		Rotator* weaponPtr = &weaponRotation;
		const Rotator savedPawn = pawnRotation;
		const Rotator savedWeapon = weaponRotation;
		pawnRotation = replacement;
		weaponRotation = replacement;
		return [pawnPtr, weaponPtr, savedPawn, savedWeapon]()
		{
			*pawnPtr = savedPawn;
			*weaponPtr = savedWeapon;
		};
	}

	bool RunWebXRWeaponAimSelfTest()
	{
		using Kind = WebXRWeaponAimScopeKind;
		auto expect = [](const WebXRWeaponCallMetadata& call, Kind kind)
		{
			return ClassifyWebXRWeaponCall(call) == kind;
		};
		auto global = [](const char* packageName, const char* className, const char* functionName)
		{
			return WebXRWeaponCallMetadata{ packageName, className, {}, "Firing", functionName };
		};
		auto state = [](const char* className, const char* stateName, const char* activeState, const char* functionName)
		{
			return WebXRWeaponCallMetadata{ "Botpack", className, stateName, activeState, functionName };
		};

		if (!expect(global("Engine", "Weapon", "TraceFire"), Kind::Ballistic) ||
			!expect(global("Engine", "Weapon", "ProjectileFire"), Kind::Ballistic) ||
			!expect(global("Botpack", "UT_FlakCannon", "Fire"), Kind::Ballistic) ||
			!expect(global("Botpack", "UT_FlakCannon", "AltFire"), Kind::Ballistic) ||
			!expect(state("UT_Eightball", "FireRockets", "FireRockets", "BeginState"), Kind::Ballistic) ||
			!expect(global("Botpack", "UT_Eightball", "CheckTarget"), Kind::TargetAcquisition) ||
			!expect(global("Botpack", "Translocator", "ThrowTarget"), Kind::Ballistic) ||
			!expect(global("Botpack", "ChainSaw", "Slash"), Kind::Ballistic) ||
			!expect(global("Botpack", "ImpactHammer", "TraceAltFire"), Kind::Ballistic) ||
			!expect(state("ImpactHammer", "Firing", "Firing", "Tick"), Kind::Ballistic) ||
			!expect(global("Botpack", "TournamentWeapon", "RenderOverlays"), Kind::Presentation) ||
			!expect(global("Engine", "Weapon", "RenderOverlays"), Kind::Presentation) ||
			!expect(global("CustomWeapons", "ModWeapon", "RenderOverlays"), Kind::Presentation))
			return false;

		const WebXRWeaponCallMetadata negatives[] = {
			global("Botpack", "TournamentWeapon", "Fire"),
			global("Engine", "Actor", "Tick"),
			state("UT_Eightball", "NormalFire", "NormalFire", "Tick"),
			state("ImpactHammer", "ClientFiring", "ClientFiring", "Tick"),
			state("ImpactHammer", "Firing", "ClientFiring", "Tick"),
			global("WrongPackage", "UT_FlakCannon", "Fire"),
			global("Botpack", "FlakCannon", "Fire"),
			state("WrongClass", "FireRockets", "FireRockets", "BeginState"),
			global("Botpack", "GuidedWarShell", "Tick"),
			state("TournamentWeapon", "Firing", "Firing", "RenderOverlays"),
			global("Engine", "Weapon", "CalcDrawOffset")
		};
		for (const auto& negative : negatives)
		{
			if (!expect(negative, Kind::None))
				return false;
		}

		// Model the exact nested LIFO behavior used by Frame::Call without
		// loading any game package: inner cleanup restores the outer override,
		// and outer cleanup restores the byte-identical baseline.
		Rotator value(101, 202, 303);
		const Rotator baseline = value;
		auto outer = BeginWebXRRotationOverride(value, Rotator(1001, 2002, 0));
		const Rotator outerValue = value;
		auto inner = BeginWebXRRotationOverride(value, Rotator(3003, 4004, 0));
		inner();
		if (value != outerValue)
			return false;
		outer();
		if (value.Pitch != baseline.Pitch || value.Yaw != baseline.Yaw || value.Roll != baseline.Roll)
			return false;

		// Presentation scopes replace both rotations and restore the exact integer
		// fields in LIFO order, including values outside normalized 16-bit range.
		Rotator pawnRotation(0x12345, -0x23456, 0x34567);
		Rotator weaponRotation(-0x45678, 0x56789, -0x6789a);
		const Rotator pawnBaseline = pawnRotation;
		const Rotator weaponBaseline = weaponRotation;
		auto presentationOuter = BeginWebXRPresentationRotationOverride(
			pawnRotation, weaponRotation, Rotator(111, 222, 333));
		auto presentationInner = BeginWebXRPresentationRotationOverride(
			pawnRotation, weaponRotation, Rotator(444, 555, 666));
		presentationInner();
		if (pawnRotation.Pitch != 111 || pawnRotation.Yaw != 222 || pawnRotation.Roll != 333 ||
			weaponRotation.Pitch != 111 || weaponRotation.Yaw != 222 || weaponRotation.Roll != 333)
			return false;
		presentationOuter();
		return pawnRotation.Pitch == pawnBaseline.Pitch && pawnRotation.Yaw == pawnBaseline.Yaw &&
			pawnRotation.Roll == pawnBaseline.Roll && weaponRotation.Pitch == weaponBaseline.Pitch &&
			weaponRotation.Yaw == weaponBaseline.Yaw && weaponRotation.Roll == weaponBaseline.Roll;
	}
#endif

	Rotator WebXRRotatorFromBasis(const vec3& forward, const vec3& right, const vec3& up)
	{
		constexpr float unitsPerRadian = 65536.0f / (2.0f * 3.14159265359f);
		const float horizontal = std::sqrt(forward.x * forward.x + forward.y * forward.y);
		float yaw = 0.0f;
		float roll = 0.0f;
		if (horizontal > 0.00001f)
		{
			yaw = std::atan2(forward.y, forward.x);
			roll = std::atan2(-right.z, up.z);
		}
		else
		{
			// At the Euler singularity yaw and roll describe the same remaining
			// degree of freedom. Choose zero roll and retain an equivalent basis.
			yaw = std::atan2(-right.x, right.y);
		}
		const float pitch = std::atan2(forward.z, horizontal);
		return normalize(Rotator(
			static_cast<int>(std::lround(pitch * unitsPerRadian)),
			static_cast<int>(std::lround(yaw * unitsPerRadian)),
			static_cast<int>(std::lround(roll * unitsPerRadian))));
	}

	void ComposeWebXRWorldPoseValues(Engine::VRTrackedPoseState& target,
		const vec3& localPosition, const vec3& localForward,
		const vec3& localRight, const vec3& localUp,
		const vec3& cameraAnchor, const Coords& bodyRotation)
	{
		if (!target.Tracked)
			return;

		target.WorldPosition = cameraAnchor +
			bodyRotation.XAxis * localPosition.x +
			bodyRotation.YAxis * localPosition.y +
			bodyRotation.ZAxis * localPosition.z;
		target.WorldForward = normalize(
			bodyRotation.XAxis * localForward.x +
			bodyRotation.YAxis * localForward.y +
			bodyRotation.ZAxis * localForward.z);
		vec3 worldRight =
			bodyRotation.XAxis * localRight.x +
			bodyRotation.YAxis * localRight.y +
			bodyRotation.ZAxis * localRight.z;
		worldRight -= target.WorldForward * dot(target.WorldForward, worldRight);
		target.WorldRight = normalize(worldRight);
		target.WorldUp = normalize(cross(target.WorldForward, target.WorldRight));
		const vec3 expectedUp = normalize(
			bodyRotation.XAxis * localUp.x +
			bodyRotation.YAxis * localUp.y +
			bodyRotation.ZAxis * localUp.z);
		if (dot(target.WorldUp, expectedUp) < 0.0f)
		{
			target.WorldRight = -target.WorldRight;
			target.WorldUp = -target.WorldUp;
		}
		target.WorldRotation = normalize(Rotator::FromVector(target.WorldForward));
		target.WorldPresentationRotation = WebXRRotatorFromBasis(
			target.WorldForward, target.WorldRight, target.WorldUp);
	}

	bool RunWebXRControllerPoseSelfTest()
	{
		Engine::VRTrackedPoseState pose;
		pose.Tracked = true;
		const vec3 anchor(10.0f, 20.0f, 30.0f);
		const Coords body = Coords::Rotation(Rotator(0, 16384, 0));
		const Coords localBasis = Coords::Rotation(Rotator(3000, 5000, 9000));
		ComposeWebXRWorldPoseValues(pose, vec3(2.0f, 3.0f, 4.0f),
			localBasis.XAxis, localBasis.YAxis, localBasis.ZAxis, anchor, body);
		const float epsilon = 0.0005f;
		const vec3 expectedPosition = anchor +
			body.XAxis * 2.0f + body.YAxis * 3.0f + body.ZAxis * 4.0f;
		const vec3 expectedForward = normalize(
			body.XAxis * localBasis.XAxis.x + body.YAxis * localBasis.XAxis.y + body.ZAxis * localBasis.XAxis.z);
		const vec3 expectedRight = normalize(
			body.XAxis * localBasis.YAxis.x + body.YAxis * localBasis.YAxis.y + body.ZAxis * localBasis.YAxis.z);
		const vec3 expectedUp = normalize(cross(expectedForward, expectedRight));
		const Rotator expectedRotation = normalize(Rotator::FromVector(expectedForward));
		const Coords presentationBasis = Coords::Rotation(pose.WorldPresentationRotation);
		const Coords singularBasis = Coords::Rotation(Rotator(-16384, 4000, 7000));
		const Coords singularRoundTrip = Coords::Rotation(WebXRRotatorFromBasis(
			singularBasis.XAxis, singularBasis.YAxis, singularBasis.ZAxis));
		if (length(pose.WorldPosition - expectedPosition) > epsilon ||
			length(pose.WorldForward - expectedForward) > epsilon ||
			length(pose.WorldRight - expectedRight) > epsilon ||
			length(pose.WorldUp - expectedUp) > epsilon ||
			std::fabs(dot(pose.WorldForward, pose.WorldRight)) > epsilon ||
			std::fabs(dot(pose.WorldForward, pose.WorldUp)) > epsilon ||
			std::fabs(dot(pose.WorldRight, pose.WorldUp)) > epsilon ||
			length(presentationBasis.XAxis - pose.WorldForward) > epsilon ||
			length(presentationBasis.YAxis - pose.WorldRight) > epsilon ||
			length(presentationBasis.ZAxis - pose.WorldUp) > epsilon ||
			length(singularRoundTrip.XAxis - singularBasis.XAxis) > epsilon ||
			length(singularRoundTrip.YAxis - singularBasis.YAxis) > epsilon ||
			length(singularRoundTrip.ZAxis - singularBasis.ZAxis) > epsilon ||
			pose.WorldRotation != expectedRotation || pose.WorldRotation.Roll != 0 ||
			pose.WorldPresentationRotation.Roll == 0)
			return false;

		// An invalid/disconnected pose remains at its safe defaults even when
		// nonzero local values are presented, preventing stale hand state.
		Engine::VRTrackedPoseState invalid;
		ComposeWebXRWorldPoseValues(invalid, vec3(99.0f), vec3(0.0f, 1.0f, 0.0f),
			vec3(1.0f, 0.0f, 0.0f), vec3(0.0f, 0.0f, 1.0f), anchor, body);
		return invalid.WorldPosition == vec3(0.0f) &&
			invalid.WorldForward == vec3(1.0f, 0.0f, 0.0f) &&
			invalid.WorldRight == vec3(0.0f, 1.0f, 0.0f) &&
			invalid.WorldUp == vec3(0.0f, 0.0f, 1.0f) &&
			invalid.WorldRotation == Rotator(0, 0, 0) &&
			invalid.WorldPresentationRotation == Rotator(0, 0, 0);
	}

#ifdef __EMSCRIPTEN__
	void ComposeWebXRWorldPose(Engine::VRTrackedPoseState& target,
		const WebXRInputPose& source, const vec3& cameraAnchor, const Coords& bodyRotation)
	{
		ComposeWebXRWorldPoseValues(target,
			vec3(source.LocalPositionUU[0], source.LocalPositionUU[1], source.LocalPositionUU[2]),
			vec3(source.LocalForward[0], source.LocalForward[1], source.LocalForward[2]),
			vec3(source.LocalRight[0], source.LocalRight[1], source.LocalRight[2]),
			vec3(source.LocalUp[0], source.LocalUp[1], source.LocalUp[2]),
			cameraAnchor, bodyRotation);
	}
#endif

	uint32_t InstallWebXRDefaults(std::map<std::string, std::string>& bindings)
	{
		uint32_t installedMask = 0;
		auto install = [&](uint32_t bit, const char* destination, const char* source, const char* fallback)
		{
			if (!bindings[destination].empty())
				return;
			auto sourceIt = bindings.find(source);
			bindings[destination] = sourceIt != bindings.end() && !sourceIt->second.empty() ?
				sourceIt->second : fallback;
			if (!bindings[destination].empty())
				installedMask |= 1u << bit;
		};

		// xr-standard fixed slots, normalized by WebXRInputState:
		//   left  Joy1 trigger, Joy2 grip, Joy4 stick, Joy5 X, Joy6 Y
		//   right Joy7 trigger, Joy8 grip, Joy10 stick, Joy11 A, Joy12 B
		// Existing Joy commands always win. Defaults borrow the user's ordinary
		// keyboard/mouse command, retaining aliases and per-game customization.
		install(0, "Joy1", "RightMouse", "AltFire");
		install(1, "Joy2", "E", "Use");
		install(3, "Joy4", "C", "Duck");
		install(4, "Joy5", "MouseWheelUp", "PrevWeapon");
		install(5, "Joy6", "Escape", "ShowMenu");
		install(6, "Joy7", "LeftMouse", "Fire");
		install(7, "Joy8", "RightMouse", "AltFire");
		install(10, "Joy11", "Space", "Jump");
		install(11, "Joy12", "MouseWheelDown", "NextWeapon");
		install(12, "JoyX", "", "Axis aStrafe Speed=2.0");
		install(13, "JoyY", "", "Axis aBaseY Speed=2.0");
		return installedMask;
	}

	int ComputeWebXRTurnDelta(float axis, float timeElapsed, Engine::WebXRTurnMode mode,
		float snapDegrees, float smoothDegreesPerSecond, float snapThreshold,
		float rearmThreshold, bool& snapArmed, uint32_t& snapCount)
	{
		axis = clamp(axis, -1.0f, 1.0f);
		if (mode == Engine::WebXRTurnMode::Snap)
		{
			if (std::fabs(axis) <= rearmThreshold)
				snapArmed = true;
			if (!snapArmed || std::fabs(axis) < snapThreshold)
				return 0;

			snapArmed = false;
			snapCount++;
			// UE1 positive yaw turns left; WebXR positive stick X means right.
			return static_cast<int>(std::lround(-std::copysign(snapDegrees, axis) * WebXRYawUnitsPerDegree));
		}

		if (std::fabs(axis) <= rearmThreshold)
			snapArmed = true;
		if (mode != Engine::WebXRTurnMode::Smooth || timeElapsed <= 0.0f ||
			std::fabs(axis) < WebXRSmoothTurnDeadZone)
			return 0;
		return static_cast<int>(std::lround(-axis * smoothDegreesPerSecond *
			timeElapsed * WebXRYawUnitsPerDegree));
	}

	bool RunWebXRLocomotionSelfTest()
	{
		std::map<std::string, std::string> bindings = {
			{ "LeftMouse", "Fire" }, { "RightMouse", "AltFire" },
			{ "Space", "Jump" }, { "Escape", "ShowMenu" },
			{ "MouseWheelUp", "PreviousFromUser" },
			{ "MouseWheelDown", "NextFromUser" },
			{ "Joy7", "ExplicitUserFire" }
		};
		const uint32_t defaults = InstallWebXRDefaults(bindings);
		if (bindings["Joy7"] != "ExplicitUserFire" || (defaults & (1u << 6)) != 0 ||
			bindings["Joy11"] != "Jump" || bindings["Joy5"] != "PreviousFromUser" ||
			bindings["JoyX"] != "Axis aStrafe Speed=2.0")
			return false;
		if (std::fabs(WebXRJoystickAxisScale * 2.0f - WebXRUE1MovementScale) > 0.001f)
			return false;

		Engine::WebXRMovementReference used = Engine::WebXRMovementReference::Body;
		bool fellBack = false;
		const vec3 headRight = ResolveWebXRMovementForward(
			Engine::WebXRMovementReference::Head, true, vec3(0.0f, 2.0f, 7.0f),
			false, vec3(0.0f), used, fellBack);
		const WebXRMovementAxes headAxes = TransformWebXRMovementAxes(0.0f, 1.0f, headRight);
		if (used != Engine::WebXRMovementReference::Head || fellBack ||
			std::fabs(headAxes.Strafe - 1.0f) > 0.0001f ||
			std::fabs(headAxes.Forward) > 0.0001f)
			return false;

		const vec3 handLeft = ResolveWebXRMovementForward(
			Engine::WebXRMovementReference::DominantHand, false, vec3(0.0f),
			true, vec3(0.0f, -4.0f, -9.0f), used, fellBack);
		const WebXRMovementAxes handAxes = TransformWebXRMovementAxes(0.25f, 0.75f, handLeft);
		if (used != Engine::WebXRMovementReference::DominantHand || fellBack ||
			std::fabs(handAxes.Strafe + 0.75f) > 0.0001f ||
			std::fabs(handAxes.Forward - 0.25f) > 0.0001f)
			return false;

		const float notFinite = std::numeric_limits<float>::quiet_NaN();
		const vec3 bodyFallback = ResolveWebXRMovementForward(
			Engine::WebXRMovementReference::Head, true, vec3(notFinite, 1.0f, 0.0f),
			false, vec3(0.0f), used, fellBack);
		if (used != Engine::WebXRMovementReference::Body || !fellBack ||
			bodyFallback.x != 1.0f || bodyFallback.y != 0.0f ||
			!IsWebXRActionButtonUnbound(bindings, 10) ||
			IsWebXRActionButtonUnbound(bindings, 11))
			return false;

		const uint32_t recenterMask = WebXRActionButtonMask(10);
		const uint32_t menuMask = WebXRActionButtonMask(9);
		const WebXRActionEdges pressEdges = ComputeWebXRActionEdges(
			0, recenterMask | menuMask, 10, 9);
		const WebXRActionEdges heldEdges = ComputeWebXRActionEdges(
			recenterMask | menuMask, recenterMask | menuMask, 10, 9);
		const WebXRActionEdges releaseEdges = ComputeWebXRActionEdges(
			recenterMask | menuMask, 0, 10, 9);
		if (!pressEdges.RecenterPressed || !pressEdges.MenuPressed || pressEdges.MenuReleased ||
			heldEdges.RecenterPressed || heldEdges.MenuPressed || heldEdges.MenuReleased ||
			releaseEdges.RecenterPressed || releaseEdges.MenuPressed || !releaseEdges.MenuReleased ||
			ParseWebXRActionButton("RightStick", 0) != 10 ||
			ParseWebXRActionButton("system", 4) != 4 || WebXRActionButtonMask(13) != 0)
			return false;

		bool armed = true;
		uint32_t snapCount = 0;
		const int thirtyDegrees = static_cast<int>(std::lround(30.0f * WebXRYawUnitsPerDegree));
		if (ComputeWebXRTurnDelta(0.8f, 1.0f, Engine::WebXRTurnMode::Snap,
			30.0f, 120.0f, 0.75f, 0.35f, armed, snapCount) != -thirtyDegrees ||
			ComputeWebXRTurnDelta(1.0f, 1.0f, Engine::WebXRTurnMode::Snap,
			30.0f, 120.0f, 0.75f, 0.35f, armed, snapCount) != 0 ||
			ComputeWebXRTurnDelta(0.5f, 1.0f, Engine::WebXRTurnMode::Snap,
			30.0f, 120.0f, 0.75f, 0.35f, armed, snapCount) != 0 || armed)
			return false;
		ComputeWebXRTurnDelta(0.3f, 1.0f, Engine::WebXRTurnMode::Snap,
			30.0f, 120.0f, 0.75f, 0.35f, armed, snapCount);
		if (!armed || ComputeWebXRTurnDelta(-0.8f, 1.0f, Engine::WebXRTurnMode::Snap,
			30.0f, 120.0f, 0.75f, 0.35f, armed, snapCount) != thirtyDegrees || snapCount != 2)
			return false;

		armed = true;
		snapCount = 0;
		const int sixtyDegrees = static_cast<int>(std::lround(60.0f * WebXRYawUnitsPerDegree));
		return RunWebXRControllerPoseSelfTest() &&
			ComputeWebXRTurnDelta(1.0f, 0.5f, Engine::WebXRTurnMode::Smooth,
			30.0f, 120.0f, 0.75f, 0.35f, armed, snapCount) == -sixtyDegrees &&
			ComputeWebXRTurnDelta(0.1f, 1.0f, Engine::WebXRTurnMode::Smooth,
				30.0f, 120.0f, 0.75f, 0.35f, armed, snapCount) == 0 &&
			ComputeWebXRTurnDelta(1.0f, 1.0f, Engine::WebXRTurnMode::Disabled,
				30.0f, 120.0f, 0.75f, 0.35f, armed, snapCount) == 0;
	}
}

Engine* engine = nullptr;

Engine::Engine(GameLaunchInfo launchinfo) : LaunchInfo(launchinfo)
{
	engine = this;

	packages = std::make_unique<PackageManager>(LaunchInfo);

	std::srand((unsigned int)std::time(nullptr));

	auto transientpkg = packages->GetTransientPackage();
	auto enginepkg = packages->GetPackage("Engine");
	gameengine = UObject::Cast<UGameEngine>(transientpkg->NewObject("gameengine", enginepkg->GetClass("GameEngine"), ObjectFlags::Transient));
	audiodev = UObject::Cast<USurrealAudioDevice>(transientpkg->NewObject("audiodev", enginepkg->GetClass("SurrealAudioDevice"), ObjectFlags::Transient));
	renderdev = UObject::Cast<USurrealRenderDevice>(transientpkg->NewObject("renderdev", enginepkg->GetClass("SurrealRenderDevice"), ObjectFlags::Transient));
	netdev = UObject::Cast<USurrealNetworkDevice>(transientpkg->NewObject("netdev", enginepkg->GetClass("SurrealNetworkDevice"), ObjectFlags::Transient));
	client = UObject::Cast<USurrealClient>(transientpkg->NewObject("client", enginepkg->GetClass("SurrealClient"), ObjectFlags::Transient));
	viewport = UObject::Cast<UViewport>(transientpkg->NewObject("viewport", enginepkg->GetClass("Viewport"), ObjectFlags::Transient));
	canvas = UObject::Cast<UCanvas>(transientpkg->NewObject("canvas", enginepkg->GetClass("Canvas"), ObjectFlags::Transient));
	DefaultTexture = UObject::Cast<UTexture>(packages->GetPackage("Engine")->GetUObject("Texture", "DefaultTexture"));

	floatprop = GC::Alloc<UFloatProperty>(NameString(), nullptr, ObjectFlags::NoFlags);

	if (LaunchInfo.IsDeusEx())
	{
		auto extpkg = packages->GetPackage("Extension");
		deusExPackage = packages->GetPackage("DeusEx");
		dxgc = UObject::Cast<UGC>(transientpkg->NewObject("gc", extpkg->GetClass("GC"), ObjectFlags::Transient));
		dxgc->Canvas() = canvas;
		dxSaveInfo = UObject::Cast<UDXSaveInfo>(transientpkg->NewObject("DeusExSaveInfo", deusExPackage->GetClass("DeusExSaveInfo"), ObjectFlags::Transient));
		dxConMissionList = UObject::Cast<UConversationMissionList>(packages->GetPackage("DeusExConText")->GetUObject("ConversationMissionList", "ConMissionList"));
	}

	std::string consolestr = packages->GetIniValue("system", "Engine.Engine", "Console");
	std::string consolepkg = consolestr.substr(0, consolestr.find('.'));
	std::string consolecls = consolestr.substr(consolestr.find('.') + 1);
	console = UObject::Cast<UConsole>(transientpkg->NewObject("console", packages->GetPackage(consolepkg)->GetClass(consolecls), ObjectFlags::Transient));

	console->Viewport() = viewport;
	canvas->Viewport() = viewport;
	viewport->Console() = console;

#ifdef __EMSCRIPTEN__
	if (LaunchInfo.IsUnrealTournament())
	{
		Frame::SetCallScopeHook([this](UFunction* func, UObject* instance, const Array<ExpressionValue>&)
		{
			return EnterWebXRWeaponAimScope(func, instance);
		});
		LogMessage(std::string("WebXR weapon aim hook installed; classifier/restoration self-test=") +
			(RunWebXRWeaponAimSelfTest() ? "pass" : "FAIL"));
	}
#endif
}

Engine::~Engine()
{
#ifdef __EMSCRIPTEN__
	// The hook captures this Engine. Clear it before tearing down any member
	// that a synchronous UnrealScript call could otherwise reach.
	Frame::SetCallScopeHook({});
#endif

	if (audiodev)
		audiodev->ShutdownDevice();

	Logger::Get()->SaveLogAsPlaintext((Directory::localAppData() / "SurrealEngine/SE-Log-LastRun.txt").string());

	engine = nullptr;
}

#ifdef __EMSCRIPTEN__
std::function<void()> Engine::EnterWebXRWeaponAimScope(UFunction* func, UObject* instance)
{
	if (!LaunchInfo.IsUnrealTournament() || !func || !instance || !viewport)
		return {};

	UPlayerPawn* pawn = viewport->Actor();
	if (!pawn)
		return {};
	UWeapon* weapon = UObject::TryCast<UWeapon>(instance);
	if (!weapon || pawn->Weapon() != weapon || weapon->Owner() != pawn)
		return {};

	const int dominantIndex = WebXRInput.DominantControllerIndex;
	if (dominantIndex < 0 || dominantIndex >= static_cast<int>(WebXRInput.Controllers.size()))
		return {};
	const VRControllerInputState& dominant = WebXRInput.Controllers[dominantIndex];
	if (!dominant.Connected)
		return {};

	const WebXRWeaponCallMetadata metadata = GetWebXRWeaponCallMetadata(func, instance);
	const WebXRWeaponAimScopeKind kind = ClassifyWebXRWeaponCall(metadata);
	if (kind == WebXRWeaponAimScopeKind::None)
		return {};

	std::function<void()> restore;
	if (kind == WebXRWeaponAimScopeKind::Presentation)
	{
		// Prefer target-ray orientation so the model and ballistic ray agree;
		// grip is a deterministic presentation-only fallback when aim is absent.
		const VRTrackedPoseState* presentationPose = dominant.AimPose.Tracked ?
			&dominant.AimPose : (dominant.GripPose.Tracked ? &dominant.GripPose : nullptr);
		if (!presentationPose)
			return {};
		restore = BeginWebXRPresentationRotationOverride(pawn->ViewRotation(),
			weapon->Rotation(), presentationPose->WorldPresentationRotation);
		WebXRWeaponAim.PresentationScopeCount++;
	}
	else
	{
		if (!dominant.AimPose.Tracked)
			return {};
		// Direction only: ballistic/target calls retain a zero-roll rotator and
		// leave CalcDrawOffset, origins, and weapon presentation untouched.
		restore = BeginWebXRRotationOverride(
			pawn->ViewRotation(), dominant.AimPose.WorldRotation);
	}
	if (kind == WebXRWeaponAimScopeKind::Ballistic)
	{
		WebXRWeaponAim.BallisticScopeCount++;
		WebXRWeaponAim.HapticRequestCount++;
		const WebXRHapticHand hand = dominant.Handedness == 1 ?
			WebXRHapticHand::Left : WebXRHapticHand::Right;
		if (QueueWebXRHapticPulse(hand, 0.55f, 35))
			WebXRWeaponAim.HapticAcceptedCount++;
	}
	else if (kind == WebXRWeaponAimScopeKind::TargetAcquisition)
		WebXRWeaponAim.TargetAcquisitionScopeCount++;

	const uint32_t totalScopes = WebXRWeaponAim.BallisticScopeCount +
		WebXRWeaponAim.TargetAcquisitionScopeCount + WebXRWeaponAim.PresentationScopeCount;
	if (totalScopes == 1 || (totalScopes % 128u) == 0)
	{
		LogMessage("WebXR weapon aim scope: count=" + std::to_string(totalScopes) +
			" kind=" + (kind == WebXRWeaponAimScopeKind::Ballistic ? "ballistic" :
				(kind == WebXRWeaponAimScopeKind::TargetAcquisition ? "target" : "presentation")) +
			" call=" + metadata.PackageName + "." + metadata.ClassName +
			(metadata.DeclaringStateName.empty() ? "." : "." + metadata.DeclaringStateName + ".") +
			metadata.FunctionName);
	}

	return [this, restore = std::move(restore)]() mutable
	{
		restore();
		WebXRWeaponAim.RestoreCount++;
	};
}
#endif

#ifdef __EMSCRIPTEN__
#include <emscripten.h>

// The ordinary browser path is owned by Emscripten's window RAF. A native
// immersive session must instead render synchronously inside XRSession's RAF
// callback because XRGPUSubImage textures are frame-scoped. These exports
// transfer loop ownership without changing the engine's per-frame behavior.
static bool EngineUsesXRFrameLoop = false;

static void EngineMainLoopCallback(void* arg)
{
	Engine* eng = static_cast<Engine*>(arg);
	if (eng->quit)
	{
		emscripten_cancel_main_loop();
		eng->Shutdown();
		return;
	}
	eng->RunOneFrame();
}

// Headless smoke-test hooks (see WEBXR_IMPLEMENTATION_PLAN.md M1 "Definition
// of M1 done") - let host JS observe the RAF-driven loop is genuinely live
// and request a clean shutdown, without needing any real rendering.
extern "C"
{
	// uint32_t, not uint64_t: ccall/cwrap don't legalize i64 return values to
	// JS Number without -sWASM_BIGINT, and M1's smoke test only needs "is
	// this advancing", not the full 64-bit range.
	EMSCRIPTEN_KEEPALIVE uint32_t Surreal_GetTickCount()
	{
		return engine ? static_cast<uint32_t>(engine->tickCount) : 0;
	}

	EMSCRIPTEN_KEEPALIVE void Surreal_RequestQuit()
	{
		if (engine)
			engine->quit = true;
	}

	EMSCRIPTEN_KEEPALIVE int Surreal_SetXRFrameLoopActive(int active)
	{
		const bool requested = active != 0;
		if (requested == EngineUsesXRFrameLoop)
		{
			// `false` is also an ensure-running operation. Immersive session
			// teardown can suppress the browser RAF independently of our logical
			// ownership flag, and Emscripten's resume() safely invalidates the old
			// scheduler generation before installing a new one.
			if (!requested)
				emscripten_resume_main_loop();
			return EngineUsesXRFrameLoop ? 1 : 0;
		}

		EngineUsesXRFrameLoop = requested;
		if (EngineUsesXRFrameLoop)
			emscripten_pause_main_loop();
		else
			emscripten_resume_main_loop();
		return EngineUsesXRFrameLoop ? 1 : 0;
	}

	EMSCRIPTEN_KEEPALIVE uint32_t Surreal_RunXRFrame()
	{
		if (!EngineUsesXRFrameLoop || !engine)
			return engine ? static_cast<uint32_t>(engine->tickCount) : 0;

		EngineMainLoopCallback(engine);
		return engine ? static_cast<uint32_t>(engine->tickCount) : 0;
	}

	EMSCRIPTEN_KEEPALIVE uint32_t Surreal_GetWebXRInputProcessedGeneration()
	{
		return engine ? static_cast<uint32_t>(engine->WebXRInput.Generation) : 0;
	}

	EMSCRIPTEN_KEEPALIVE uint32_t Surreal_GetWebXRInputSourceCount()
	{
		return engine ? engine->WebXRInput.SourceCount : 0;
	}

	EMSCRIPTEN_KEEPALIVE uint32_t Surreal_GetWebXRInputButtonsHeld()
	{
		return engine ? engine->WebXRInput.SynthesizedButtonsHeld : 0;
	}

	EMSCRIPTEN_KEEPALIVE float Surreal_GetWebXRInputAxis(uint32_t axis)
	{
		return engine && axis < engine->WebXRInput.SynthesizedAxes.size() ?
			engine->WebXRInput.SynthesizedAxes[axis] : 0.0f;
	}

	EMSCRIPTEN_KEEPALIVE float Surreal_GetWebXRInputTrigger(uint32_t controller)
	{
		return engine && controller < engine->WebXRInput.Controllers.size() ?
			engine->WebXRInput.Controllers[controller].TriggerValue : 0.0f;
	}

	EMSCRIPTEN_KEEPALIVE uint32_t Surreal_GetWebXRInputPoseFlags(uint32_t controller)
	{
		if (!engine || controller >= engine->WebXRInput.Controllers.size())
			return 0;
		const auto& state = engine->WebXRInput.Controllers[controller];
		return (state.AimPose.Tracked ? 1u : 0u) |
			(state.GripPose.Tracked ? 2u : 0u) |
			(state.Connected ? 4u : 0u);
	}

	// pose: 0=grip, 1=aim. value: position XYZ (0..2), forward XYZ
	// (3..5), direction-only Rotator pitch/yaw/roll (6..8), right XYZ
	// (9..11), up XYZ (12..14), full presentation Rotator (15..17).
	// Invalid requests return zero;
	// callers should gate reads with Surreal_GetWebXRInputPoseFlags.
	EMSCRIPTEN_KEEPALIVE float Surreal_GetWebXRControllerWorldPoseValue(
		uint32_t controller, uint32_t pose, uint32_t value)
	{
		if (!engine || controller >= engine->WebXRInput.Controllers.size() || pose > 1 || value > 17)
			return 0.0f;
		const auto& controllerState = engine->WebXRInput.Controllers[controller];
		const auto& poseState = pose == 0 ? controllerState.GripPose : controllerState.AimPose;
		if (!poseState.Tracked)
			return 0.0f;
		if (value < 3)
			return poseState.WorldPosition[value];
		if (value < 6)
			return poseState.WorldForward[value - 3];
		if (value == 6)
			return static_cast<float>(poseState.WorldRotation.Pitch);
		if (value == 7)
			return static_cast<float>(poseState.WorldRotation.Yaw);
		if (value == 8)
			return static_cast<float>(poseState.WorldRotation.Roll);
		if (value < 12)
			return poseState.WorldRight[value - 9];
		if (value < 15)
			return poseState.WorldUp[value - 12];
		if (value == 15)
			return static_cast<float>(poseState.WorldPresentationRotation.Pitch);
		if (value == 16)
			return static_cast<float>(poseState.WorldPresentationRotation.Yaw);
		return static_cast<float>(poseState.WorldPresentationRotation.Roll);
	}

	EMSCRIPTEN_KEEPALIVE int Surreal_GetWebXRDominantControllerIndex()
	{
		return engine ? engine->WebXRInput.DominantControllerIndex : -1;
	}

	EMSCRIPTEN_KEEPALIVE int Surreal_RunWebXRControllerPoseSelfTest()
	{
		return RunWebXRControllerPoseSelfTest() ? 1 : 0;
	}

	EMSCRIPTEN_KEEPALIVE uint32_t Surreal_GetWebXRDefaultBindingMask()
	{
		return engine ? engine->WebXRLocomotion.DefaultBindingMask : 0;
	}

	EMSCRIPTEN_KEEPALIVE float Surreal_GetWebXRLocomotionScale()
	{
		return WebXRUE1MovementScale;
	}

	EMSCRIPTEN_KEEPALIVE uint32_t Surreal_GetWebXRTurnMode()
	{
		return engine ? static_cast<uint32_t>(engine->GetWebXRTurnMode()) : 0;
	}

	EMSCRIPTEN_KEEPALIVE int Surreal_SetWebXRTurnMode(uint32_t mode)
	{
		return engine && engine->SetWebXRTurnMode(mode) ? 1 : 0;
	}

	EMSCRIPTEN_KEEPALIVE uint32_t Surreal_GetWebXRMovementReference()
	{
		return engine ? static_cast<uint32_t>(engine->GetWebXRMovementReference()) : 0;
	}

	EMSCRIPTEN_KEEPALIVE int Surreal_SetWebXRMovementReference(uint32_t reference)
	{
		return engine && engine->SetWebXRMovementReference(reference) ? 1 : 0;
	}

	EMSCRIPTEN_KEEPALIVE uint32_t Surreal_GetWebXRDominantHand()
	{
		return engine ? engine->GetWebXRDominantHand() : 2;
	}

	EMSCRIPTEN_KEEPALIVE int Surreal_SetWebXRDominantHand(uint32_t handedness)
	{
		return engine && engine->SetWebXRDominantHand(handedness) ? 1 : 0;
	}

	EMSCRIPTEN_KEEPALIVE uint32_t Surreal_GetWebXRRecenterButton()
	{
		return engine ? engine->GetWebXRRecenterButton() : 0;
	}

	EMSCRIPTEN_KEEPALIVE int Surreal_SetWebXRRecenterButton(uint32_t button)
	{
		return engine && engine->SetWebXRRecenterButton(button) ? 1 : 0;
	}

	EMSCRIPTEN_KEEPALIVE uint32_t Surreal_GetWebXRMenuButton()
	{
		return engine ? engine->GetWebXRMenuButton() : 0;
	}

	EMSCRIPTEN_KEEPALIVE int Surreal_SetWebXRMenuButton(uint32_t button)
	{
		return engine && engine->SetWebXRMenuButton(button) ? 1 : 0;
	}

	EMSCRIPTEN_KEEPALIVE int Surreal_SetWebXRSnapTurnDegrees(float degrees)
	{
		return engine && engine->SetWebXRSnapTurnDegrees(degrees) ? 1 : 0;
	}

	EMSCRIPTEN_KEEPALIVE int Surreal_SetWebXRSmoothTurnDegreesPerSecond(float degreesPerSecond)
	{
		return engine && engine->SetWebXRSmoothTurnDegreesPerSecond(degreesPerSecond) ? 1 : 0;
	}

	EMSCRIPTEN_KEEPALIVE uint32_t Surreal_GetWebXRSnapTurnCount()
	{
		return engine ? engine->WebXRLocomotion.SnapTurnCount : 0;
	}

	EMSCRIPTEN_KEEPALIVE int Surreal_GetWebXRLastTurnDelta()
	{
		return engine ? engine->WebXRLocomotion.LastTurnDelta : 0;
	}

	EMSCRIPTEN_KEEPALIVE float Surreal_GetWebXRLastMoveForward()
	{
		return engine ? engine->WebXRLocomotion.LastMoveForward : 0.0f;
	}

	EMSCRIPTEN_KEEPALIVE float Surreal_GetWebXRLastMoveStrafe()
	{
		return engine ? engine->WebXRLocomotion.LastMoveStrafe : 0.0f;
	}

	EMSCRIPTEN_KEEPALIVE uint32_t Surreal_GetWebXRMovementReferenceUsed()
	{
		return engine ? static_cast<uint32_t>(
			engine->WebXRLocomotion.LastMovementReferenceUsed) : 0;
	}

	EMSCRIPTEN_KEEPALIVE uint32_t Surreal_GetWebXRMovementFallbackCount()
	{
		return engine ? engine->WebXRLocomotion.MovementFallbackCount : 0;
	}

	EMSCRIPTEN_KEEPALIVE uint32_t Surreal_GetWebXREffectiveRecenterButton()
	{
		return engine ? engine->WebXRLocomotion.EffectiveRecenterButton : 0;
	}

	EMSCRIPTEN_KEEPALIVE uint32_t Surreal_GetWebXREffectiveMenuButton()
	{
		return engine ? engine->WebXRLocomotion.EffectiveMenuButton : 0;
	}

	EMSCRIPTEN_KEEPALIVE uint32_t Surreal_GetWebXRRecenterActionCount()
	{
		return engine ? engine->WebXRLocomotion.RecenterActionCount : 0;
	}

	EMSCRIPTEN_KEEPALIVE uint32_t Surreal_GetWebXRMenuActionCount()
	{
		return engine ? engine->WebXRLocomotion.MenuActionCount : 0;
	}

	EMSCRIPTEN_KEEPALIVE int Surreal_RunWebXRLocomotionSelfTest()
	{
		return RunWebXRLocomotionSelfTest() ? 1 : 0;
	}

	EMSCRIPTEN_KEEPALIVE int Surreal_RunWebXRWeaponAimSelfTest()
	{
		return RunWebXRWeaponAimSelfTest() ? 1 : 0;
	}

	EMSCRIPTEN_KEEPALIVE uint32_t Surreal_GetWebXRWeaponAimBallisticScopeCount()
	{
		return engine ? engine->WebXRWeaponAim.BallisticScopeCount : 0;
	}

	EMSCRIPTEN_KEEPALIVE uint32_t Surreal_GetWebXRWeaponAimTargetScopeCount()
	{
		return engine ? engine->WebXRWeaponAim.TargetAcquisitionScopeCount : 0;
	}

	EMSCRIPTEN_KEEPALIVE uint32_t Surreal_GetWebXRWeaponAimPresentationScopeCount()
	{
		return engine ? engine->WebXRWeaponAim.PresentationScopeCount : 0;
	}

	EMSCRIPTEN_KEEPALIVE uint32_t Surreal_GetWebXRWeaponAimRestoreCount()
	{
		return engine ? engine->WebXRWeaponAim.RestoreCount : 0;
	}

	EMSCRIPTEN_KEEPALIVE uint32_t Surreal_GetWebXRWeaponAimHapticRequestCount()
	{
		return engine ? engine->WebXRWeaponAim.HapticRequestCount : 0;
	}

	EMSCRIPTEN_KEEPALIVE uint32_t Surreal_GetWebXRWeaponAimHapticAcceptedCount()
	{
		return engine ? engine->WebXRWeaponAim.HapticAcceptedCount : 0;
	}

	EMSCRIPTEN_KEEPALIVE uint32_t Surreal_GetWebXRWeaponOverlayExpectedEyePasses()
	{
		return engine ? engine->render->GetWebXRWeaponOverlayDiagnostics().LastFrameExpectedEyePasses : 0;
	}

	EMSCRIPTEN_KEEPALIVE uint32_t Surreal_GetWebXRWeaponOverlayEyePasses()
	{
		return engine ? engine->render->GetWebXRWeaponOverlayDiagnostics().LastFrameEyePasses : 0;
	}

	EMSCRIPTEN_KEEPALIVE uint32_t Surreal_GetWebXRWeaponOverlayCalls()
	{
		return engine ? engine->render->GetWebXRWeaponOverlayDiagnostics().LastFrameWeaponCalls : 0;
	}

	EMSCRIPTEN_KEEPALIVE int Surreal_SetWebXRHapticsEnabled(int enabled)
	{
		return SetWebXRHapticsEnabled(enabled != 0) ? 1 : 0;
	}

	EMSCRIPTEN_KEEPALIVE int Surreal_RunWebXRHapticsBridgeSelfTest()
	{
		return RunWebXRHapticsBridgeSelfTest() ? 1 : 0;
	}
}
#endif

void Engine::Run()
{
	Setup();
#ifdef __EMSCRIPTEN__
	EngineUsesXRFrameLoop = false;
	// simulate_infinite_loop=0: matches QuakeQuest's main_web.c reference -
	// this returns immediately after registering the RAF callback rather
	// than unwinding the stack via a JS-level throw (simulate_infinite_loop=1
	// relies on that unwind to keep stack-allocated locals like `this` alive,
	// which isn't reliable here). GameApp.cpp gives the Engine static storage
	// duration so it survives this function returning.
	emscripten_set_main_loop_arg(EngineMainLoopCallback, this, 0, 0);
#else
	while (!quit)
		RunOneFrame();
	Shutdown();
#endif
}

void Engine::Setup()
{
	LogMessage("Game: " + LaunchInfo.gameName + " (Version: " + LaunchInfo.gameVersionString + ")");
	LoadEngineSettings();
	LogMessage("Loaded Engine settings");
	LoadKeybindings();
	LogMessage("Loaded key bindings");
	LogGamePackageSHA1Sums();

	OpenWindow();

	audiodev->InitDevice();
	render = std::make_unique<RenderSubsystem>(window->GetRenderDevice());

	if (commandline && commandline->HasArg("", "--debugfixedsize"))
	{
		// Non-interactive diagnostic: proves the scene render target can be
		// pinned to a size independent of the OS window (needed for VR,
		// where the OpenXR swapchain resolution has nothing to do with the
		// desktop mirror window size), without needing a real OpenXR
		// session. See VR_IMPLEMENTATION_PLAN.md M2 step 8.
		std::string sizeArg = commandline->GetArg("", "--debugfixedsize");
		size_t xpos = sizeArg.find('x');
		if (xpos != std::string::npos)
		{
			int w = std::atoi(sizeArg.substr(0, xpos).c_str());
			int h = std::atoi(sizeArg.substr(xpos + 1).c_str());
			if (w > 0 && h > 0)
				render->Device->SetFixedRenderSize(w, h);
		}
	}

	if (engine->LaunchInfo.ue1Version > 219 && !client->StartupFullscreen)
		viewport->bWindowsMouseAvailable() = true;

	window->LockCursor();

	if (packages->IsKlingonHonorGuard())
	{
		PlayAVI({ "playavi", "INTRO.AVI", "N" });
	}

	if (!LaunchInfo.noEntryMap)
		LoadEntryMap();

	if (LaunchInfo.url.empty())
		LoadMap(GetDefaultURL(packages->GetIniValue("system", "URL", "LocalMap")));
	else
		LoadMap(UnrealURL(GetDefaultURL(packages->GetIniValue("system", "URL", "LocalMap")), LaunchInfo.url));

	LoginPlayer();

	runLoopObjProp = GC::Alloc<UObjectProperty>(NameString(), nullptr, ObjectFlags::NoFlags);
	runLoopVecProp = GC::Alloc<UStructProperty>(NameString(), nullptr, ObjectFlags::NoFlags);
	runLoopRotProp = GC::Alloc<UStructProperty>(NameString(), nullptr, ObjectFlags::NoFlags);
}

void Engine::RunOneFrame()
{
	const float levelElapsed = AdvanceGameFrame();
	RenderGameFrame(levelElapsed);
	FinishGameFrame(levelElapsed);
}

float Engine::AdvanceGameFrame()
{
	auto& objprop = runLoopObjProp;
	auto& vecprop = runLoopVecProp;
	auto& rotprop = runLoopRotProp;

	tickCount++;

	float realTimeElapsed = CalcTimeElapsed();
	float entryLevelElapsed = EntryLevel ? realTimeElapsed * clamp(EntryLevelInfo->TimeDilation(), 0.0025f, 25.0f) : 0.0f;
	float levelElapsed = realTimeElapsed * clamp(LevelInfo->TimeDilation(), 0.0025f, 25.0f);

	TotalTime += realTimeElapsed;

	if (EntryLevel)
		EntryLevelInfo->TimeSeconds() += entryLevelElapsed;
	LevelInfo->TimeSeconds() += levelElapsed;
	Logger::Get()->SetTimeSeconds(LevelInfo->TimeSeconds());

	// Update the time fields
	std::time_t now = std::time(nullptr);
	std::tm* timedesc = std::localtime(&now);

	LevelInfo->Year() = timedesc->tm_year;
	LevelInfo->Month() = timedesc->tm_mon;
	LevelInfo->Day() = timedesc->tm_mday;
	LevelInfo->DayOfWeek() = timedesc->tm_wday;
	LevelInfo->Hour() = timedesc->tm_hour;
	LevelInfo->Minute() = timedesc->tm_min;
	LevelInfo->Second() = timedesc->tm_sec;
	LevelInfo->Millisecond() = 0; // No timedesc equivalent for LevelInfo->Millisecond()

	UpdateInput(realTimeElapsed);

	SetPause(!LevelInfo->Pauser().empty());

	// Do NOT pause this Tick event otherwise some messages will stay on screen forever.
	CallEvent(console, EventName::Tick, { ExpressionValue::FloatValue(levelElapsed) });

	// To do: set these to true if the frame rate is too low
	if (LaunchInfo.ue1Version >= 436)
	{
		LevelInfo->bDropDetail() = false;
		LevelInfo->bAggressiveLOD() = false;
	}

	if (EntryLevel)
		EntryLevel->Tick(entryLevelElapsed, m_GamePaused);
	Level->Tick(levelElapsed, m_GamePaused);

	if (dxRootWindow)
		dxRootWindow->Tick(levelElapsed); // Should this maybe be realTimeElapsed?

	// To do: improve CallEvent so parameter passing isn't this painful
	UFunction* funcPlayerCalcView = viewport->Actor() ? FindEventFunction(viewport->Actor(), "PlayerCalcView") : nullptr;
	if (funcPlayerCalcView)
	{
		vecprop->Struct = UObject::Cast<UStructProperty>(funcPlayerCalcView->Properties[1])->Struct;
		rotprop->Struct = UObject::Cast<UStructProperty>(funcPlayerCalcView->Properties[2])->Struct;
		CameraActor = viewport->Actor();
		CameraLocation = viewport->Actor()->Location();
		CameraRotation = viewport->Actor()->Rotation();
		CameraFovAngle = viewport->Actor()->FovAngle();
		CallEvent(viewport->Actor(), EventName::PlayerCalcView, {
			ExpressionValue::Variable(&CameraActor, objprop),
			ExpressionValue::Variable(&CameraLocation, vecprop),
			ExpressionValue::Variable(&CameraRotation, rotprop)
			});
		HasCalculatedCameraView = true;
	}

	UpdateAudio();
	return levelElapsed;
}

void Engine::RenderGameFrame(float levelElapsed)
{
	viewport->SetViewportRect(0, 0, engine->window->GetPixelWidth(), engine->window->GetPixelHeight());
	render->DrawGame(levelElapsed);
}

void Engine::FinishGameFrame(float levelElapsed)
{
	// Save the game if there is a request for it
	if (SaveGameInfo.SaveGameSlot != DONT_SAVE_GAME)
	{
		SaveGameToSlot(SaveGameInfo.SaveGameSlot, SaveGameInfo.SaveGameDescription);

		SaveGameInfo.SaveGameSlot = DONT_SAVE_GAME;
		SaveGameInfo.SaveGameDescription.clear();
	}

	// Check if there is a new map to load
	if (!LevelInfo->NextURL().empty())
	{
		LevelInfo->NextSwitchCountdown() -= levelElapsed;
		if (LevelInfo->NextSwitchCountdown() <= 0.0f)
		{
			if (UnrealURL(LevelInfo->NextURL()).HasOption("restart"))
			{
				LoadMap(LevelInfo->URL, Level->TravelInfo);
				LoginPlayer();
			}
			else if (LevelInfo->bNextItems())
			{
				LoadMap(UnrealURL(LevelInfo->URL, LevelInfo->NextURL()), CreateTravelInfo(true));
				LoginPlayer();
			}
			else
			{
				LoadMap(UnrealURL(LevelInfo->URL, LevelInfo->NextURL()), {});
				LoginPlayer();
			}
		}
	}

	if (ClientTravelInfo.URL.HasOption("restart"))
	{
		LoadMap(LevelInfo->URL, Level->TravelInfo);
		LoginPlayer();
	}

	if (ClientTravelInfo.URL.HasOption("load"))
	{
		UnrealURL url(ClientTravelInfo.URL);
		LoadFromSaveFile(url);
		LoginPlayer();
	}

	if (!ClientTravelInfo.URL.Map.empty())
	{
		// To do: need to do something about that travel type and transfering of items

		UnrealURL url(ClientTravelInfo.URL);
		LogMessage("Client travel to " + url.ToString());
		LoadMap(url, CreateTravelInfo(ClientTravelInfo.TransferItems));
		LoginPlayer();
	}
}

void Engine::Shutdown()
{
	LogMessage("Shutting down...");
	window->UnlockCursor();

	LogMessage("Saving configurations...");
	if (packages->MissingSESystemIni())
	{
		// Add the missing Subsystem entries
		client->SaveConfig();
		audiodev->SaveConfig();
		renderdev->SaveConfig();
	}
	SaveWebXRInputSettings();
	packages->SetIniValue("System", "Engine.SurrealWindowSystem", "WindowSystem", windowingSystemName);
	packages->SaveAllIniFiles();

	LogMessage("Closing window...");
	CloseWindow();
}

void Engine::PlayAVI(const Array<std::string>& args)
{
	if (args.size() < 3)
		return;

	// What did KHG request?
	std::string buildup, breakdown, video;
	if (args.size() > 4 && args[2] == "C")
	{
		buildup = args[1];
		video = args[3];
		breakdown = "breakdn.avi";
	}
	else if (args.size() > 3 && args[2] == "Y")
	{
		buildup = "buildup.avi";
		video = args[1];
		breakdown = "breakdn.avi";
	}
	else
	{
		video = args[1];
	}

	playingAvi = true;
	skipAvi = false;

	try
	{
		// Load the videos

		std::unique_ptr<VideoPlayer> buildupPlayer, videoPlayer, breakdownPlayer;
		if (!buildup.empty())
			buildupPlayer = VideoPlayer::Create(packages->GetVideoFilename(buildup));
		if (!video.empty())
			videoPlayer = VideoPlayer::Create(packages->GetVideoFilename(video));
		if (!breakdown.empty())
			breakdownPlayer = VideoPlayer::Create(packages->GetVideoFilename(breakdown));

		CalcTimeElapsed(); // Reset so load time doesn't affect playback

		UnrealMipmap* background = nullptr;
		if (buildupPlayer)
		{
			background = PlayVideo(buildupPlayer.get(), nullptr);
			if (!background)
			{
				CalcTimeElapsed();
				return;
			}

			// Cut a hole in the background image
			uint32_t* pixels = (uint32_t*)background->Data.data();
			int w = background->Width;
			int h = background->Height;

			ivec2 v0 = { 232, 125 };
			ivec2 v1 = { 161, 246 };
			ivec2 v2 = { 406, 125 };
			ivec2 v3 = { 475, 246 };
			ivec2 v4 = { 258, 125 };
			ivec2 v5 = { 270, 144 };
			ivec2 v6 = { 380, 125 };
			ivec2 v7 = { 368, 144 };
			ivec2 v8 = { 210, 327 };
			ivec2 v9 = { 427, 327 };

			for (int y = 126; y < 144; y++)
			{
				int x0 = (int)(v0.x + 0.5f + (y - v0.y + 0.5f) * (v1.x - v0.x) / (v1.y - v0.y));
				int x1 = (int)(v4.x + 0.5f + (y - v4.y + 0.5f) * (v5.x - v4.x) / (v5.y - v4.y));
				int x2 = (int)(v6.x + 0.5f + (y - v6.y + 0.5f) * (v7.x - v6.x) / (v7.y - v6.y));
				int x3 = (int)(v2.x + 0.5f + (y - v2.y + 0.5f) * (v3.x - v2.x) / (v3.y - v2.y));

				pixels[x0 + y * w] = 0x80000000;
				pixels[x1 - 1 + y * w] = 0x80000000;
				for (int x = x0 + 1; x < x1 - 1; x++)
					pixels[x + y * w] = 0;

				pixels[x2 + y * w] = 0x80000000;
				pixels[x3 - 1 + y * w] = 0x80000000;
				for (int x = x2 + 1; x < x3 - 1; x++)
					pixels[x + y * w] = 0;
			}

			for (int y = 144; y < 246; y++)
			{
				int x0 = (int)(v0.x + 0.5f + (y - v0.y + 0.5f) * (v1.x - v0.x) / (v1.y - v0.y));
				int x1 = (int)(v2.x + 0.5f + (y - v2.y + 0.5f) * (v3.x - v2.x) / (v3.y - v2.y));
				pixels[x0 + y * w] = 0x80000000;
				pixels[x1 - 1 + y * w] = 0x80000000;
				for (int x = x0 + 1; x < x1 - 1; x++)
					pixels[x + y * w] = 0;
			}

			for (int y = 246; y < 325; y++)
			{
				int x0 = (int)(v1.x + 0.5f + (y - v1.y + 0.5f) * (v8.x - v1.x) / (v8.y - v1.y));
				int x1 = (int)(v3.x + 0.5f + (y - v3.y + 0.5f) * (v9.x - v3.x) / (v9.y - v3.y));
				pixels[x0 + y * w] = 0x80000000;
				pixels[x1 - 1 + y * w] = 0x80000000;
				for (int x = x0 + 1; x < x1 - 1; x++)
					pixels[x + y * w] = 0;
			}
		}

		if (videoPlayer)
		{
			if (!PlayVideo(videoPlayer.get(), background))
			{
				playingAvi = false;
				CalcTimeElapsed();
				return;
			}
		}

		if (breakdownPlayer)
		{
			PlayVideo(breakdownPlayer.get(), nullptr);
		}
	}
	catch (const std::exception& e)
	{
		LogMessage("Error playing " + video + ": " + e.what());
	}

	playingAvi = false;
	CalcTimeElapsed(); // Reset so game isn't affected
}

UnrealMipmap* Engine::PlayVideo(VideoPlayer* video, UnrealMipmap* background)
{
	UnrealMipmap* frame = nullptr;

	FTextureInfo texinfo[2];
	texinfo[0].CacheID = 0xffffffff'ffffffffULL;
	texinfo[0].Format = TextureFormat::BGRA8;
	texinfo[0].NumMips = 1;

	if (background)
	{
		texinfo[1].CacheID = 0xffffffff'fffffffeULL;
		texinfo[1].Format = TextureFormat::BGRA8;
		texinfo[1].NumMips = 1;
		texinfo[1].Mips = background;
		texinfo[1].USize = background->Width;
		texinfo[1].VSize = background->Height;
		texinfo[1].bRealtimeChanged = true;
	}

	audiodev->SetViewport(nullptr);
	audiodev->GetDevice()->PlayMusic(video->GetAudio());

	float timestamp = 0.0f;
	int curframe = -1;
	while (!quit && !skipAvi)
	{
		timestamp += CalcTimeElapsed();

		bool done = false;
		while (curframe < video->GetFrameIndexForTime(timestamp))
		{
			while (true)
			{
				UnrealMipmap* nextframe = video->NextVideoFrame();
				if (nextframe)
				{
					frame = nextframe;
					curframe++;
					texinfo[0].bRealtimeChanged = true;
					break;
				}
				if (!video->Decode())
				{
					done = true;
					break;
				}
			}
			if (done)
				break;
		}
		if (done)
			break;

		audiodev->GetDevice()->Update();
		GameWindow::ProcessEvents();

		if (frame)
		{
			texinfo[0].Mips = frame;
			texinfo[0].USize = frame->Width;
			texinfo[0].VSize = frame->Height;

			viewport->SetViewportRect(0, 0, engine->window->GetPixelWidth(), engine->window->GetPixelHeight());
			render->DrawVideoFrame(&texinfo[0], background ? &texinfo[1] : nullptr);
		}
	}

	audiodev->GetDevice()->PlayMusic(nullptr);

	if (quit || skipAvi)
		return nullptr;

	return frame;
}

UConversationList* Engine::GetDeusExMission()
{
	if (!dxConMissionList || !DeusExLevelInfo)
		return nullptr;

	int missionNumber = DeusExLevelInfo->MissionNumber();
	for (UConItem* item = dxConMissionList->missions(); item; item = item->Next())
	{
		auto mission = UObject::Cast<UConversationList>(item->ConObject());
		if (mission->missionNumber() == missionNumber)
		{
			return mission;
		}
	}
	return nullptr;
}

void Engine::UpdateAudio()
{
	mat4 translate = mat4::translate(vec3(0.0f) - CameraLocation);
	mat4 listener = Coords::ViewToAudioDev().ToMatrix() * Coords::Rotation(CameraRotation).ToMatrix() * translate;

	audiodev->SetViewport(viewport);
	audiodev->Update(listener);
}

void Engine::ClientTravel(const std::string& newURL, ETravelType travelType, bool transferItems)
{
	UnrealURL url(newURL);

	// If the URL doesn't contain the player info, add them here.
	// As they have to persist somehow
	for (std::string optionKey : { "Name", "Class", "team", "skin", "Face", "Voice", "OverrideClass" })
	{
		if (engine->LaunchInfo.ue1Version > 219)
		{
			if (url.HasOption(optionKey))
				engine->packages->SetIniValue("User", "DefaultPlayer", optionKey, url.GetOption(optionKey));
			else
				url.AddOrReplaceOption(optionKey + "=" + packages->GetIniValue("user", "DefaultPlayer", optionKey));
		}
		else
		{
			if (url.HasOption(optionKey))
				engine->packages->SetIniValue("System", "URL", optionKey, url.GetOption(optionKey));
			else
				url.AddOrReplaceOption(optionKey + "=" + packages->GetIniValue("System", "URL", optionKey));
		}
	}

	if (travelType == ETravelType::TRAVEL_Absolute)
		ClientTravelInfo.URL = url;
	else if (travelType == ETravelType::TRAVEL_Partial)
	{
		auto name = ClientTravelInfo.URL.GetOption("name");
		ClientTravelInfo.URL = url;
		ClientTravelInfo.URL.AddOrReplaceOption("name=" + name);
	}
	else if (travelType == ETravelType::TRAVEL_Relative)
		ClientTravelInfo.URL = UnrealURL(ClientTravelInfo.URL, url);
	ClientTravelInfo.TravelType = travelType;
	ClientTravelInfo.TransferItems = transferItems;
}

UnrealURL Engine::GetDefaultURL(const std::string& map)
{
	UnrealURL url;
	std::string teleporterTag = "";
	std::string finalMapName = map;

	size_t tagPos = map.find('#');

	if (tagPos != std::string::npos)
	{
		teleporterTag = map.substr(tagPos + 1);
		finalMapName = map.substr(0, tagPos);
	}
	if (map.find("." + packages->GetMapExtension()) == std::string::npos)
		url.Map = finalMapName + "." + packages->GetMapExtension();
	else
		url.Map = finalMapName;

	if (!teleporterTag.empty())
		url.Portal = teleporterTag;
	for (std::string optionKey : { "Name", "Class", "team", "skin", "Face", "Voice", "OverrideClass" })
	{
		url.Options.push_back(optionKey + "=" + packages->GetIniValue("user", "DefaultPlayer", optionKey));
	}
	return url;
}

void Engine::LoadEntryMap()
{
	// The entry map is the map you see in the game when no other map is playing. For example when disconnected from a server. It is always loaded and running.
	const auto entryMapName = packages->GetIniValue("System", "URL", "EntryMap", "Entry");
	LoadMap(GetDefaultURL(entryMapName));
	EntryLevelInfo = LevelInfo;
	EntryLevel = Level;
	EntryLevelPackage = std::move(LevelPackage);
	LevelInfo = nullptr;
	Level = nullptr;
	viewport->Actor() = nullptr;
}

void Engine::UnloadMap()
{
	if (!LevelPackage)
		return;

	LevelInfo = nullptr;
	if (packages->IsDeusEx())
		DeusExLevelInfo = nullptr;
	Level = nullptr;
	viewport->Actor() = nullptr;
	dxRootWindow = nullptr;
	packages->UnloadPackage(std::move(LevelPackage));
}

void Engine::LoadMap(const UnrealURL& url, const std::map<std::string, std::string>& travelInfo)
{
	ClientTravelInfo.URL.Clear();

	if (Level)
		CallEvent(console, EventName::NotifyLevelChange);

	if (url.HasOption("entry")) // Not sure what the purpose of this kind of travel is - do nothing for now.
		return;

	audiodev->StopSounds();
	UnloadMap();

	// Load map objects

	LevelPackage = packages->LoadMap(url.Map);

	GetLevelInfoObject();

	LevelInfo->ComputerName() = "MyComputer";
	LevelInfo->HubStackLevel() = 0; // To do: handle level hubs
	LevelInfo->EngineVersion() = LaunchInfo.gameVersionString + " SE";
	if (LaunchInfo.ue1Version > 219)
		LevelInfo->MinNetVersion() = LaunchInfo.gameVersionString + " SE";
	LevelInfo->bHighDetailMode() = true;
	LevelInfo->NetMode() = 0; // NM_StandAlone
	LevelInfo->DefaultTexture() = engine->DefaultTexture;

	LevelInfo->URL = url;

	GetLevelObject();

	Level->TravelInfo = travelInfo; // Initially used travel info for level restart

	// Remove the actors meant for the editor (to do: should we do this at the package manager level?)
	for (UActor*& actor : Level->Actors)
	{
		if (actor && AllFlags(actor->Flags, ObjectFlags::NotForServer))
		{
			actor->bDeleteMe() = true;
			actor = nullptr;
		}
	}

	LinkActorsToLevel();

	// Find the game info class
	UClass* gameInfoClass = packages->FindClass(LevelInfo->URL.GetOption("game"));
	if (!gameInfoClass)
		gameInfoClass = LevelInfo->DefaultGameType();
	if (!gameInfoClass)
		gameInfoClass = packages->FindClass(packages->GetIniValue("system", "Engine.Engine", "DefaultGame"));
	if (!gameInfoClass)
		gameInfoClass = packages->FindClass("Botpack.DeathMatchPlus");
	if (!gameInfoClass)
		Exception::Throw("Could not find any gameinfo class!");

	// Spawn GameInfo actor
	GameInfo = UObject::Cast<UGameInfo>(LevelPackage->NewObject("gameinfo", gameInfoClass, ObjectFlags::NoFlags));
	GameInfo->XLevel() = Level;
	GameInfo->Level() = LevelInfo;
	Level->Collision.AddToCollision(GameInfo);
	Level->Light.AddLight(GameInfo);
	GameInfo->Tag() = gameInfoClass->Name;
	GameInfo->bTicked() = false;
	GameInfo->InitActorZone();
	GameInfo->Index = (int)Level->Actors.size();
	Level->Actors.push_back(GameInfo);

	LevelInfo->Game() = GameInfo;

	if (!LevelInfo->bBegunPlay())
	{
		LevelInfo->TimeSeconds() = 0.0f;
		LevelInfo->bBegunPlay() = true;

		std::string options = url.GetOptions();

		auto stringProp = GC::Alloc<UStringProperty>("", nullptr, ObjectFlags::NoFlags);
		std::string error;

		// Only call PreBegin/Begin/PostBegin/SetInitialState for loaded objects. Spawned objects are added at the end of the Actors array.
		size_t loadActorCount = Level->Actors.size();

		LevelInfo->bStartup() = true;
		CallEvent(GameInfo, EventName::InitGame, { ExpressionValue::StringValue(options), ExpressionValue::Variable(&error, stringProp) });
		if (!error.empty())
			Exception::Throw("InitGame failed: " + error);

		// Note: the events may spawn actors. We can't use iterators here.
		for (size_t i = 0; i < loadActorCount; i++) { if (Level->Actors[i]) CallEvent(Level->Actors[i], EventName::PreBeginPlay); }
		for (size_t i = 0; i < loadActorCount; i++) { if (Level->Actors[i]) CallEvent(Level->Actors[i], EventName::BeginPlay); }
		for (size_t i = 0; i < loadActorCount; i++) { if (Level->Actors[i]) CallEvent(Level->Actors[i], EventName::PostBeginPlay); }
		for (size_t i = 0; i < loadActorCount; i++) { if (Level->Actors[i]) CallEvent(Level->Actors[i], EventName::SetInitialState); }

		if (engine->LaunchInfo.IsDeusEx())
		{
			for (size_t i = 0; i < loadActorCount; i++) { if (Level->Actors[i]) CallEvent(Level->Actors[i], "PostPostBeginPlay"); }
		}

		for (size_t i = 0; i < loadActorCount; i++) { if (Level->Actors[i]) Level->Actors[i]->InitBase(); }
		LevelInfo->bStartup() = false;
	}

	if (LevelInfo->Game())
		CallEvent(LevelInfo->Game(), "DetailChange", {});
}

void Engine::LoadFromSaveFile(const UnrealURL& url)
{
	ClientTravelInfo.URL.Clear();

	if (Level)
		CallEvent(console, EventName::NotifyLevelChange);

	if (url.HasOption("entry")) // Not sure what the purpose of this kind of travel is - do nothing for now.
		return;

	Package* savefilePackage = nullptr;

	if (url.HasOption("load"))
	{
		uint32_t slotNum = Convert::to_uint32(url.GetOption("load"));
		savefilePackage = packages->LoadSaveSlot(slotNum);
	}

	if (!savefilePackage)
		return;

	audiodev->StopSounds();
	UnloadMap();

	LevelPackage = savefilePackage;

	GetLevelInfoObject();

	/*
	LevelInfo->ComputerName() = "MyComputer";
	LevelInfo->HubStackLevel() = 0; // To do: handle level hubs
	*/
	LevelInfo->EngineVersion() = LaunchInfo.gameVersionString + " SE";
	if (LaunchInfo.ue1Version > 219)
		LevelInfo->MinNetVersion() = LaunchInfo.gameVersionString + " SE";
	LevelInfo->bHighDetailMode() = true;
	/*
	LevelInfo->NetMode() = 0; // NM_StandAlone
	LevelInfo->DefaultTexture() = engine->DefaultTexture;
	*/

	GetLevelObject();

	LinkActorsToLevel();
}

void Engine::SaveGameToSlot(int32_t slotNum, const std::string& saveDescription) const
{
	if (slotNum < -1 || (!packages->IsDeusEx() && slotNum < 0))
		Exception::Throw("Invalid save slot: " + std::to_string(slotNum));

	// First and foremost ensure the Save folder exists
	const auto saveFolderPath = packages->GetSaveFolderPath();
	if (!fs::exists(saveFolderPath))
		fs::create_directory(saveFolderPath);

	if (packages->IsDeusEx())
	{
		// Saving a game on Deus Ex does the following:
		// - Create a folder using the slotNum (e.g. 1 -> "Save0001")
		// - Save the level package using the name [MapName].dxs
		// - Save the associated DeusExSaveInfo class as SaveInfo.dxs within that same folder,
		// in which saveDescription parameter will be used in DeusExSaveInfo.Description
		auto slotNumStr = std::to_string(slotNum);
		slotNumStr.insert(0, 4 - slotNumStr.length(), '0'); // Pad it with 0s
		auto saveFolder = "Save" + slotNumStr;

		auto saveSlotFolder = saveFolderPath / saveFolder;
		if (!fs::exists(saveSlotFolder) || !fs::is_directory(saveSlotFolder))
			fs::create_directory(saveSlotFolder);

		auto levelName = Level->package->GetPackageName().ToString() + "." + packages->GetSaveExtension();
		auto saveInfoName = "SaveInfo." + packages->GetSaveExtension();
		auto saveFileFullPath = (saveSlotFolder / levelName).string();
		auto saveInfoFullPath = (saveSlotFolder / saveInfoName).string();
		LevelPackage->Save(Level, saveFileFullPath);

		dxSaveInfo->DirectoryIndex() = slotNum;
		dxSaveInfo->Description() = saveDescription;
		dxSaveInfo->MissionLocation() = DeusExLevelInfo ? DeusExLevelInfo->MissionLocation() : "";
		dxSaveInfo->MapName() = Level->package->GetPackageName().ToString();
		dxSaveInfo->UpdateTimeStamp();
		deusExPackage->Save(dxSaveInfo, saveInfoFullPath);
	}
	else
	{
		const std::string saveFileName = "Save" + std::to_string(slotNum) + "." + packages->GetSaveExtension();
		const std::string saveFileFullPath = (saveFolderPath / saveFileName).string();
		LevelPackage->Save(Level, saveFileFullPath);
	}
}

std::map<std::string, std::string> Engine::CreateTravelInfo(bool transferItems)
{
	auto travelInfo = Level->TravelInfo;
	for (UActor* actor : Level->Actors)
	{
		UPlayerPawn* pawn = UObject::TryCast<UPlayerPawn>(actor);
		if (pawn && pawn->Player())
		{
			std::string playerName = engine->LaunchInfo.ue1Version > 219 ? pawn->PlayerReplicationInfo()->PlayerName() : std::string("Player"); // To do: how to get the travel player name?
			travelInfo[playerName] = ActorTravelInfo::Create(pawn, transferItems);
		}
	}
	return travelInfo;
}

void Engine::LoginPlayer()
{
	// The next XR input frame must not anchor a newly logged-in player's hands
	// to the prior pawn/map's scripted camera result.
	HasCalculatedCameraView = false;

	UnrealURL url = LevelInfo->URL;
	std::map<std::string, std::string> travelInfo = Level->TravelInfo;

	auto stringProp = GC::Alloc<UStringProperty>("", nullptr, ObjectFlags::NoFlags);
	std::string error, failcode;

	std::string portal = url.GetPortal();
	std::string options = url.GetOptions();

	std::string playerPawnClass = url.GetOption("Class");
	if (playerPawnClass.empty())
		playerPawnClass = packages->GetIniValue("system", "URL", "Class");
	UClass* pawnClass = packages->FindClass(playerPawnClass);

	// Perform PreLogin check (used for early rejection in network games)
	CallEvent(LevelInfo->Game(), EventName::PreLogin, {
		ExpressionValue::StringValue(options),
		ExpressionValue::Variable(&error, stringProp),
		ExpressionValue::Variable(&failcode, stringProp),
		});
	if (!error.empty() || !failcode.empty())
		Exception::Throw("GameInfo prelogin failed: " + error + " (" + failcode + ")");

	// Create viewport pawn
	size_t numActors = Level->Actors.size();
	UPlayerPawn* pawn = UObject::Cast<UPlayerPawn>(CallEvent(LevelInfo->Game(), EventName::Login, {
		ExpressionValue::StringValue(portal),
		ExpressionValue::StringValue(options),
		ExpressionValue::Variable(&error, stringProp),
		ExpressionValue::ObjectValue(pawnClass)
		}).ToObject());
	if (!pawn || !error.empty())
		Exception::Throw("GameInfo login failed: " + error);
	bool actorActuallySpawned = Level->Actors.size() != numActors;

	pawn->LoadProperties();

	if (auto pawnExt = UObject::TryCast<UPlayerPawnExt>(pawn))
	{
		// Unclear if this is how DeusEx spawned this object
		if (!pawnExt->FlagBase())
		{
			auto flagBaseCls = packages->FindClass("Extension.FlagBase");
			pawnExt->FlagBase() = UObject::Cast<UFlagBase>(packages->GetTransientPackage()->NewObject("FlagBase", flagBaseCls, ObjectFlags::Transient));
		}
	}

	// Assign the pawn to the viewport
	viewport->Actor() = pawn;
	viewport->Actor()->Player() = viewport;
	CallEvent(viewport->Actor(), EventName::Possess);

	// Transfer travel actors to the new map

	CallEvent(pawn, EventName::TravelPreAccept);

	Array<UActor*> acceptedActors;
	if (actorActuallySpawned && ClientTravelInfo.TravelType == ETravelType::TRAVEL_Relative)
	{
		std::string playerName = url.GetOption("Name");
		if (playerName.empty())
			playerName = packages->GetIniValue("system", "URL", "Name");

		auto it = travelInfo.find(playerName);
		if (!playerName.empty() && it != travelInfo.end())
		{
			acceptedActors = ActorTravelInfo::Accept(pawn, it->second);
		}
		else
		{
			if (travelInfo.empty())
				LogMessage("Skipping travel transfer. No travel data");
			else if (playerName.empty())
				LogMessage("Skipping travel transfer. Player name is empty");
			else
				LogMessage("Skipping travel transfer. Player '" + playerName + "' not found in travel info");
		}
	}

	for (UActor* actor : acceptedActors)
		CallEvent(actor, EventName::TravelPreAccept);

	CallEvent(LevelInfo->Game(), EventName::AcceptInventory, { ExpressionValue::ObjectValue(pawn) });

	for (UActor* actor : acceptedActors)
		CallEvent(actor, EventName::TravelPostAccept);

	CallEvent(pawn, EventName::TravelPostAccept);
	CallEvent(LevelInfo->Game(), EventName::PostLogin, { ExpressionValue::ObjectValue(pawn) });

	render->OnMapLoaded();
}

UZoneInfo* Engine::GetZoneActor(int zoneIndex)
{
	if (auto zone = UObject::TryCast<UZoneInfo>(Level->Model->Zones[zoneIndex].ZoneActor))
		return zone;
	else
		return LevelInfo;
}

UObject* Engine::FindObject(NameString name, NameString className)
{
	for (auto actor : Level->Actors)
	{
		if (actor && actor->Name == name && UObject::GetUClassFullName(actor) == className)
			return actor;
	}

	return nullptr;
}

float Engine::CalcTimeElapsed()
{
	using namespace std::chrono;

	uint64_t currentTime = duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count();
	if (lastTime == 0)
		lastTime = currentTime;

	uint64_t deltaTime = currentTime - lastTime;
	lastTime = currentTime;
	return clamp(deltaTime / 1'000'000.0f, 0.0f, 1.0f);
}

std::string Engine::ParseClassName(std::string className)
{
	// Workaround for broken unrealscript code referencing windrv.windowsclient directly
	if (className == "windrv.windowsclient")
	{
		className = "ini:Engine.ViewportManager";
	}

	if (className.size() < 4 || className.substr(0, 4) != "ini:")
		return className;

	size_t pos = className.find_last_of('.');
	if (pos == std::string::npos)
		Exception::Throw("Parse error");

	NameString sectionName = className.substr(4, pos - 4);
	NameString keyName = className.substr(pos + 1);

	// Override the ini file for things that are internal in Surreal Engine
	if (sectionName == "Engine.Engine")
	{
		if (keyName == "GameRenderDevice" || keyName == "WindowedRenderDevice")
		{
			return renderdev->Class;
		}
		else if (keyName == "AudioDevice")
		{
			return audiodev->Class;
		}
		else if (keyName == "NetworkDevice")
		{
			return netdev->Class;
		}
		else if (keyName == "ViewportManager")
		{
			return client->Class;
		}
	}

	return packages->GetIniValue("system", sectionName, keyName);
}

std::string Engine::ConsoleCommand(UObject* context, const std::string& commandline, BitfieldBool& found)
{
	found = false;

	Array<std::string> args = GetArgs(commandline);
	if (args.empty())
	{
		return {};
	}

	std::string command = args[0];
	for (char& c : command) c = std::tolower(c);

	found = true;
	if (command == "exit" || command == "quit")
	{
		quit = true;
	}
	else if (command == "timedemo" && args.size() == 2)
	{
		render->ShowTimedemoStats = args[1] == "1";
	}
	else if (command == "stat" && args.size() == 2)
	{
		render->ShowRenderStats = 0;

		if (args[1] == "render")
			render->ShowRenderStats = 1;
	}
	else if (command == "collisiondebug" && args.size() == 2)
	{
		render->ShowCollisionDebug = args[1] == "1";
	}
	else if (command == "dxwindowdebug" && LaunchInfo.IsDeusEx())
	{
		m_DrawDebugDXWindowHierarchy = !m_DrawDebugDXWindowHierarchy;
	}
	else if (command == "showlog")
	{
		//Frame::ShowDebuggerWindow();
	}
	/*else if (command == "playsong")
	{
		auto music = LevelInfo->Song();
		if (music)
			audio->PlayMusic(AudioSource::CreateMod(music->Data, true, 0, LevelInfo->SongSection()));
	}
	else if (command == "stopsong")
	{
		audio->PlayMusic(nullptr);
	}*/
	else if (command == "getres")
	{
		return window->GetAvailableResolutions();
	}
	else if (command == "getcolordepths")
	{
		return "32 16";
	}
	else if (command == "getcurrentres")
	{
		int width = window->GetPixelWidth();
		int height = window->GetPixelHeight();

		return std::to_string(width) + "x" + std::to_string(height);
	}
	else if (command == "getcurrentcolordepth")
	{
		return "32";
	}
	else if (command == "getping")
	{
		return "0";
	}
	else if (command == "getloss")
	{
		return "0";
	}
	else if (command == "keyname" && args.size() == 2)
	{
		uint8_t index = Convert::to_uint8(args[1]);
		return keynames[index];
	}
	else if (command == "keybinding" && args.size() == 2)
	{
		const std::string& name = args[1];
		return keybindings[name];
	}
	else if ((command == "open" || command == "start") && args.size() == 2)
	{
		const std::string& maparg = args[1];

		UnrealURL url(maparg);

		for (auto& map : packages->GetMaps())
		{
			std::string mapname = fs::path(map).stem().string();

			if (StrTools::equals_ignore_case(mapname, url.Map))
			{
				ClientTravel(url.ToString(), ETravelType::TRAVEL_Absolute, false);
				return {};
			}	
		}

		LogMessage("Couldn't find map " + maparg);
	}
	else if (command == "switchlevel" && args.size() == 2)
	{
		// This works like open/start, but keeps the difficulty level
		// As well as the game type
		const std::string& maparg = args[1];

		UnrealURL url(maparg);

		// First check if the provided URL has a difficulty option
		std::string difficulty = url.GetOption("difficulty");

		// If there isn't, try to get it from the current level's options
		if (difficulty.empty())
		{
			difficulty = LevelInfo->URL.GetOption("difficulty");
			if (!difficulty.empty())
				url.AddOrReplaceOption("difficulty=" + difficulty);
		}

		// If there still isn't, try to figure the current difficulty out using LevelInfo
		if (difficulty.empty())
		{
			if (LevelInfo->bDifficulty0())
				difficulty = "0";
			else if (LevelInfo->bDifficulty1())
				difficulty = "1";
			else if (LevelInfo->bDifficulty2())
				difficulty = "2";
			else if (LevelInfo->bDifficulty3())
				difficulty = "3";
			else
				difficulty = "2"; // Assume "Normal" difficulty

			url.AddOrReplaceOption("difficulty=" + difficulty);
		}

		for (auto& map : packages->GetMaps())
		{
			std::string mapname = fs::path(map).stem().string();

			if (StrTools::equals_ignore_case(mapname, url.Map))
			{
				LevelInfo->NextURL() = url.ToString();
				return {};
			}
		}

		LogMessage("Couldn't find map " + maparg);
	}
	else if (command == "savegame" && (args.size() == 2 || args.size() == 3))
	{

		int32_t slotNum;
		// slotNum not being parsable shouldn't cause a crash
		try
		{
			slotNum = Convert::to_int32(args[1]);
		}
		catch (...)
		{
			return {};
		}

		SaveGameInfo.SaveGameSlot = slotNum;

		if (args.size() == 3)
			SaveGameInfo.SaveGameDescription = args[2];

		// SaveGameToSlot(slotNum, "");

		//LogMessage("SaveGame command not fully implemented yet!");
		return {};
	}
	else if (command == "get" && args.size() == 3)
	{
		NameString className = ParseClassName(args[1]);
		NameString propertyName = args[2];

		UClass* cls = packages->FindClass(className);
		if (!cls)
		{
			LogMessage("Could not find class '" + className.ToString() + "': " + commandline);
			return {};
		}

		if (className == renderdev->Class)
		{
			return renderdev->GetPropertyAsString(propertyName);
		}
		else if (className == audiodev->Class)
		{
			return audiodev->GetPropertyAsString(propertyName);
		}
		else if (className == netdev->Class)
		{
			return netdev->GetPropertyAsString(propertyName);
		}
		else if (className == client->Class)
		{
			return client->GetPropertyAsString(propertyName);
		}
		else
		{
			try
			{
				return cls->GetPropertyAsString(propertyName);
			}
			catch (const std::exception&)
			{
				LogMessage("Could not get property '" + propertyName.ToString() + "': " + commandline);
				return {};
			}
		}
	}
	else if (command == "set" && args.size() == 4)
	{
		NameString className = ParseClassName(args[1]);
		NameString propertyName = args[2];
		std::string value = args[3];

		// Special input setting handling
		if (className == "input")
		{
			keybindings[propertyName.ToString()] = value;
			packages->SetIniValue("user", "Engine.Input", propertyName, value);
			RefreshWebXRActionBindings();
			return {};
		}

		UClass* cls = packages->FindClass(className);
		if (!cls)
		{
			LogMessage("Could not find class '" + className.ToString() + "': " + commandline);
			return {};
		}

		if (className == renderdev->Class)
		{
			renderdev->SetPropertyFromString(propertyName, value);
		}
		else if (className == audiodev->Class)
		{
			audiodev->SetPropertyFromString(propertyName, value);
		}
		else if (className == netdev->Class)
		{
			netdev->SetPropertyFromString(propertyName, value);
		}
		else if (className == client->Class)
		{
			client->SetPropertyFromString(propertyName, value);
		}
		else
		{
			try
			{
				cls->SetPropertyFromString(propertyName, value);
			}
			catch (const std::exception&)
			{
				LogMessage("Could not set property '" + propertyName.ToString() + "': " + commandline);
			}
			return {};
		}
	}
	else if (command == "setres" && args.size() == 2)
	{
		window->SetResolution(args[1]);
	}
	else if (command == "togglefullscreen")
	{
		bool isFullscreen = window->IsFullscreen();

		// Get the resolution to SWITCH TO
		int width = isFullscreen ? client->WindowedViewportX : client->FullscreenViewportX;
		int height = isFullscreen ? client->WindowedViewportY : client->FullscreenViewportY;

		Size resolution;
		resolution.width = width;
		resolution.height = height;

		window->ToggleWindowFullscreen(resolution);
		viewport->SetViewportRect(0, 0, width, height);

		return {};
	}
	else if (command == "prsq")
	{
		// Klingon Honor Guard: CD check
		// "mpgameplay"
		// "mpinstall"
		return "mpgameplay";
	}
	else if (command == "os")
	{
		// 227 Seems to have this command
#ifdef WIN32
		return "Windows";
#elif __APPLE__
		return "macOS";
#else
		return "Linux"; // With apologies to BSD, Haiku and others...
#endif
	}
	else if (command == "getsplash")
	{
		return khgSplashScreen ? "true" : "false";
	}
	else if (command == "setsplash")
	{
		khgSplashScreen = true;
		return {};
	}
	else if (command == "playavi")
	{
		PlayAVI(args);
		return {};
	}
	else if (command == "flush")
	{
		engine->render->Device->Flush(1);
		return {};
	}
	else
	{
		if (!ExecCommand(args))
		{
			LogMessage("Unknown command: " + commandline);
			found = false;
		}
	}
	return {};
}

Array<std::string> Engine::GetArgs(const std::string& commandline)
{
	Array<std::string> args;
	size_t i = 0;
	while (i < commandline.size())
	{
		size_t j = commandline.find_first_not_of(" \t", i);
		if (j == std::string::npos)
			break;
		i = j;
		j = commandline.find_first_of(" \t", i);
		if (j == std::string::npos)
			j = commandline.size();
		if (j > i)
			args.push_back(commandline.substr(i, j - i));
		i = j;
	}
	return args;
}

Array<std::string> Engine::GetSubcommands(const std::string& command)
{
	Array<std::string> subcommands;
	size_t pos = 0;
	while (pos < command.size())
	{
		size_t endpos = command.find('|', pos);
		if (endpos == std::string::npos)
			endpos = command.size();

		std::string subcommand = command.substr(pos, endpos - pos);
		if (!subcommand.empty())
			subcommands.push_back(subcommand);
		pos = endpos + 1;
	}
	return subcommands;
}

void Engine::LoadEngineSettings()
{
	if (packages->MissingSESystemIni())
	{
		client->LoadProperties("WinDrv.WindowsClient");
		audiodev->LoadProperties("Galaxy.GalaxyAudioSubsystem");
		renderdev->LoadProperties("D3DDrv.Direct3DRenderDevice");
	}
	else
	{
		client->LoadProperties();
		audiodev->LoadProperties();
		renderdev->LoadProperties();
	}

#ifdef WIN32
	windowingSystemName = packages->GetIniValue("System", "Engine.SurrealWindowSystem", "WindowSystem", "Win32");
#else
	windowingSystemName = packages->GetIniValue("System", "Engine.SurrealWindowSystem", "WindowSystem", "SDL2");
#endif
}

bool Engine::SetWebXRTurnMode(uint32_t mode)
{
	if (mode > static_cast<uint32_t>(WebXRTurnMode::Disabled))
		return false;
	WebXRTurnModeSetting = static_cast<WebXRTurnMode>(mode);
	WebXRSnapTurnArmed = true;
	return true;
}

bool Engine::SetWebXRMovementReference(uint32_t reference)
{
	if (reference > static_cast<uint32_t>(WebXRMovementReference::DominantHand))
		return false;
	WebXRMovementReferenceSetting = static_cast<WebXRMovementReference>(reference);
	return true;
}

bool Engine::SetWebXRDominantHand(uint32_t handedness)
{
	if (handedness != 1 && handedness != 2)
		return false;
	WebXRInput.DominantHandedness = handedness;
	WebXRInput.DominantControllerIndex = -1;
	return true;
}

bool Engine::SetWebXRRecenterButton(uint32_t button)
{
	if (button > WebXRActionButtonCount)
		return false;
	WebXRRecenterButtonSetting = button;
	RefreshWebXRActionBindings();
	return true;
}

bool Engine::SetWebXRMenuButton(uint32_t button)
{
	if (button > WebXRActionButtonCount)
		return false;
	WebXRMenuButtonSetting = button;
	RefreshWebXRActionBindings();
	return true;
}

bool Engine::SetWebXRSnapTurnDegrees(float degrees)
{
	if (!std::isfinite(degrees) || degrees < 15.0f || degrees > 90.0f)
		return false;
	WebXRSnapTurnDegrees = degrees;
	return true;
}

bool Engine::SetWebXRSmoothTurnDegreesPerSecond(float degreesPerSecond)
{
	if (!std::isfinite(degreesPerSecond) || degreesPerSecond < 15.0f || degreesPerSecond > 360.0f)
		return false;
	WebXRSmoothTurnDegreesPerSecond = degreesPerSecond;
	return true;
}

void Engine::LoadWebXRInputSettings()
{
#ifdef __EMSCRIPTEN__
	std::string movementReference = packages->GetIniValue(
		"user", "Engine.WebXR", "MovementReference", "Body");
	for (char& c : movementReference)
		c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	if (movementReference == "head" || movementReference == "headset")
		WebXRMovementReferenceSetting = WebXRMovementReference::Head;
	else if (movementReference == "dominanthand" || movementReference == "hand" ||
		movementReference == "controller")
		WebXRMovementReferenceSetting = WebXRMovementReference::DominantHand;
	else
		WebXRMovementReferenceSetting = WebXRMovementReference::Body;

	std::string dominantHand = packages->GetIniValue(
		"user", "Engine.WebXR", "DominantHand", "Right");
	for (char& c : dominantHand)
		c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	WebXRInput.DominantHandedness = dominantHand == "left" || dominantHand == "1" ? 1 : 2;

	WebXRRecenterButtonSetting = ParseWebXRActionButton(packages->GetIniValue(
		"user", "Engine.WebXR", "RecenterButton", "RightStick"), 10);
	WebXRMenuButtonSetting = ParseWebXRActionButton(packages->GetIniValue(
		"user", "Engine.WebXR", "MenuButton", "None"), 0);

	std::string turnMode = packages->GetIniValue("user", "Engine.WebXR", "TurnMode", "Snap");
	for (char& c : turnMode)
		c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	if (turnMode == "smooth")
		WebXRTurnModeSetting = WebXRTurnMode::Smooth;
	else if (turnMode == "binding" || turnMode == "legacy")
		WebXRTurnModeSetting = WebXRTurnMode::Binding;
	else if (turnMode == "disabled" || turnMode == "off")
		WebXRTurnModeSetting = WebXRTurnMode::Disabled;
	else
		WebXRTurnModeSetting = WebXRTurnMode::Snap;

	auto readFloat = [&](const char* key, float fallback, float minimum, float maximum)
	{
		const std::string text = packages->GetIniValue("user", "Engine.WebXR", key, std::to_string(fallback));
		const float value = static_cast<float>(std::atof(text.c_str()));
		return std::isfinite(value) && value >= minimum && value <= maximum ? value : fallback;
	};
	WebXRSnapTurnDegrees = readFloat("SnapTurnDegrees", 30.0f, 15.0f, 90.0f);
	WebXRSmoothTurnDegreesPerSecond = readFloat("SmoothTurnDegreesPerSecond", 120.0f, 15.0f, 360.0f);
	WebXRSnapTurnThreshold = readFloat("SnapTurnThreshold", 0.75f, 0.5f, 0.95f);
	WebXRSnapTurnRearmThreshold = readFloat("SnapTurnRearmThreshold", 0.35f, 0.05f, 0.49f);
	if (WebXRSnapTurnRearmThreshold >= WebXRSnapTurnThreshold)
		WebXRSnapTurnRearmThreshold = 0.35f;

	std::string hapticsEnabled = packages->GetIniValue(
		"user", "Engine.WebXR", "HapticsEnabled", "True");
	for (char& c : hapticsEnabled)
		c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	const bool enableHaptics = hapticsEnabled != "false" && hapticsEnabled != "0" &&
		hapticsEnabled != "off" && hapticsEnabled != "no";
	SetWebXRHapticsEnabled(enableHaptics);
	RefreshWebXRActionBindings();
#endif
}

void Engine::SaveWebXRInputSettings()
{
#ifdef __EMSCRIPTEN__
	if (!packages)
		return;
	const char* movementReference = "Body";
	if (WebXRMovementReferenceSetting == WebXRMovementReference::Head)
		movementReference = "Head";
	else if (WebXRMovementReferenceSetting == WebXRMovementReference::DominantHand)
		movementReference = "DominantHand";
	packages->SetIniValue("user", "Engine.WebXR", "MovementReference", movementReference);
	packages->SetIniValue("user", "Engine.WebXR", "DominantHand",
		WebXRInput.DominantHandedness == 1 ? "Left" : "Right");
	packages->SetIniValue("user", "Engine.WebXR", "RecenterButton",
		WebXRActionButtonName(WebXRRecenterButtonSetting));
	packages->SetIniValue("user", "Engine.WebXR", "MenuButton",
		WebXRActionButtonName(WebXRMenuButtonSetting));
#endif
}

void Engine::RefreshWebXRActionBindings()
{
	WebXRLocomotion.EffectiveRecenterButton =
		IsWebXRActionButtonUnbound(keybindings, WebXRRecenterButtonSetting) ?
		WebXRRecenterButtonSetting : 0;
	WebXRLocomotion.EffectiveMenuButton =
		IsWebXRActionButtonUnbound(keybindings, WebXRMenuButtonSetting) ?
		WebXRMenuButtonSetting : 0;
	// One physical edge must never toggle the menu and recenter at once.
	if (WebXRLocomotion.EffectiveMenuButton == WebXRLocomotion.EffectiveRecenterButton)
		WebXRLocomotion.EffectiveMenuButton = 0;
}

void Engine::InstallWebXRDefaultBindings()
{
#ifdef __EMSCRIPTEN__
	WebXRLocomotion.DefaultBindingMask = InstallWebXRDefaults(keybindings);
#endif
}

void Engine::LoadKeybindings()
{
	for (int i = 0; i < 256; i++)
	{
		std::string keyname = keynames[i];
		keybindings[keyname] = packages->GetIniValue("user", "Engine.Input", keyname);
	}

	for (int i = 0; i < 40; i++)
	{
		std::string alias = packages->GetIniValue("user", "Engine.Input", "Aliases[" + std::to_string(i) + "]");

		// Total trash parsing, but it will do for the aliases I have! Feel free to improve it!
		std::string commandStart = "(Command=\"";
		std::string commandSplit = "\",Alias=";
		std::string commandEnd = ")";
		if (alias.size() > commandStart.size() + commandSplit.size() + commandEnd.size())
		{
			size_t pos = alias.find(commandSplit, commandStart.size());
			if (pos != std::string::npos)
			{
				size_t pos2 = alias.find(commandEnd, pos);
				if (pos2 != std::string::npos)
				{
					std::string aliasCommand = alias.substr(commandStart.size(), pos - commandStart.size());
					std::string aliasName = alias.substr(pos + commandSplit.size(), pos2 - pos - commandSplit.size());
					if (!aliasName.empty() && aliasName != "None")
						inputAliases[aliasName] = aliasCommand;
				}
			}
		}
	}

	// WebXR defaults are runtime fallbacks only: never overwrite or persist an
	// existing Joy command. They remain remappable through the ordinary
	// `set input JoyN ...` path and the user's Engine.Input section.
	InstallWebXRDefaultBindings();
	LoadWebXRInputSettings();
}

void Engine::UpdateInput(float timeElapsed)
{
	if (timeElapsed <= 0.0f)
		return;

	TickWindow();
	if (tickDebugger)
		tickDebugger();

	if (!viewport->Actor())
		return;

	UpdateWebXRInput(timeElapsed);

	for (auto& it : activeInputButtons)
		viewport->Actor()->SetBool(it.first, true);
	for (auto& it : activeInputAxes)
	{
		if (it.first == "aMouseX" || it.first == "aMouseY")
		{
			viewport->Actor()->SetFloat(it.first, it.second.Value / (timeElapsed * 150.0f));
		}
		else
		{
			viewport->Actor()->SetFloat(it.first, it.second.Value);
		}
	}
}

void Engine::UpdateWebXRInput(float timeElapsed)
{
#ifdef __EMSCRIPTEN__
	const WebXRInputSnapshot snapshot = GetLatestWebXRInputSnapshot();
	if (HasProcessedWebXRInput && snapshot.FrameGeneration == LastProcessedWebXRInputGeneration)
		return;

	HasProcessedWebXRInput = true;
	LastProcessedWebXRInputGeneration = snapshot.FrameGeneration;
	WebXRInput.Generation = snapshot.FrameGeneration;
	WebXRInput.SourceCount = snapshot.SourceCount;

	for (size_t slot = 0; slot < WebXRInput.Controllers.size(); slot++)
	{
		const WebXRControllerState& source = snapshot.Controllers[slot];
		VRControllerInputState state;
		state.Connected = (source.Flags & WebXRConnected) != 0;
		state.SourceId = source.SourceId;
		state.Handedness = source.Handedness;
		state.Flags = source.Flags;
		state.ButtonsPressed = source.ButtonsPressed;
		state.ButtonsTouched = source.ButtonsTouched;
		for (size_t axis = 0; axis < state.Axes.size(); axis++)
			state.Axes[axis] = source.Axes[axis];
		for (size_t button = 0; button < state.ButtonValues.size(); button++)
			state.ButtonValues[button] = source.ButtonValues[button];
		state.TriggerValue = source.ButtonValues[0];
		state.GripPose.Tracked = (source.Flags & WebXRGripValid) != 0;
		state.GripPose.Position = vec3(source.GripPose.Position[0], source.GripPose.Position[1], source.GripPose.Position[2]);
		state.GripPose.Orientation = vec4(source.GripPose.Orientation[0], source.GripPose.Orientation[1], source.GripPose.Orientation[2], source.GripPose.Orientation[3]);
		state.AimPose.Tracked = (source.Flags & WebXRAimValid) != 0;
		state.AimPose.Position = vec3(source.AimPose.Position[0], source.AimPose.Position[1], source.AimPose.Position[2]);
		state.AimPose.Orientation = vec4(source.AimPose.Orientation[0], source.AimPose.Orientation[1], source.AimPose.Orientation[2], source.AimPose.Orientation[3]);
		WebXRInput.Controllers[slot] = state;
	}
	WebXRInput.HeadPose = {};
	WebXRInput.HeadPose.Tracked = snapshot.HeadPoseValid;

	// Hand roles are resolved from handedness, not hard-coded slot use. Slot
	// normalization normally places left/right at 0/1, while this loop keeps
	// unhanded or future snapshot layouts from producing a stale dominant pose.
	WebXRInput.DominantControllerIndex = -1;
	for (size_t slot = 0; slot < WebXRInput.Controllers.size(); slot++)
	{
		const VRControllerInputState& controller = WebXRInput.Controllers[slot];
		if (controller.Connected && controller.Handedness == WebXRInput.DominantHandedness &&
			(controller.AimPose.Tracked || controller.GripPose.Tracked))
		{
			WebXRInput.DominantControllerIndex = static_cast<int32_t>(slot);
			break;
		}
	}

	// Stable controller-local generic slots preserve the legacy remapping
	// layer: left buttons 0..5 become Joy1..Joy6, right buttons 0..5 become
	// Joy7..Joy12. Higher WebXR buttons remain available in WebXRInput.
	static constexpr EInputKey ButtonKeys[12] = {
		IK_Joy1, IK_Joy2, IK_Joy3, IK_Joy4, IK_Joy5, IK_Joy6,
		IK_Joy7, IK_Joy8, IK_Joy9, IK_Joy10, IK_Joy11, IK_Joy12
	};
	uint32_t nextButtonsHeld = 0;
	for (size_t slot = 0; slot < WebXRInput.Controllers.size(); slot++)
	{
		const VRControllerInputState& controller = WebXRInput.Controllers[slot];
		if (!controller.Connected)
			continue;
		for (uint32_t button = 0; button < 6; button++)
		{
			if ((controller.ButtonsPressed & (1u << button)) != 0)
				nextButtonsHeld |= 1u << (static_cast<uint32_t>(slot) * 6u + button);
		}
	}

	const uint32_t changedButtons = WebXRButtonsHeld ^ nextButtonsHeld;
	for (uint32_t button = 0; button < 12; button++)
	{
		const uint32_t mask = 1u << button;
		if ((changedButtons & mask) != 0)
			InputEvent(ButtonKeys[button], (nextButtonsHeld & mask) != 0 ? IST_Press : IST_Release);
	}
	const WebXRActionEdges actionEdges = ComputeWebXRActionEdges(
		WebXRButtonsHeld, nextButtonsHeld,
		WebXRLocomotion.EffectiveRecenterButton, WebXRLocomotion.EffectiveMenuButton);
	if (actionEdges.RecenterPressed)
	{
		// Invalidate the bridge's single recenter origin. The next XR frame will
		// rebuild eyes, head, and hands together from the shared origin.
		Surreal_ResetWebXRPose();
		WebXRLocomotion.RecenterActionCount++;
	}
	if (actionEdges.MenuPressed)
	{
		InputEvent(IK_Escape, IST_Press);
		WebXRLocomotion.MenuActionCount++;
	}
	if (actionEdges.MenuReleased)
		InputEvent(IK_Escape, IST_Release);
	WebXRButtonsHeld = nextButtonsHeld;
	WebXRInput.SynthesizedButtonsHeld = nextButtonsHeld;

	static constexpr EInputKey AxisKeys[4] = { IK_JoyX, IK_JoyY, IK_JoyU, IK_JoyV };
	uint32_t nextAxesActive = 0;
	for (size_t slot = 0; slot < WebXRInput.Controllers.size(); slot++)
	{
		const VRControllerInputState& controller = WebXRInput.Controllers[slot];
		const size_t output = slot * 2;
		if (!controller.Connected)
		{
			WebXRInput.SynthesizedAxes[output] = 0.0f;
			WebXRInput.SynthesizedAxes[output + 1] = 0.0f;
			continue;
		}

		// xr-standard Quest thumbsticks occupy axes 2/3. A non-XR-standard
		// generic source that only drives 0/1 gets a deterministic fallback.
		const bool useGenericPair = (controller.Flags & WebXRXRStandard) == 0 &&
			controller.Axes[2] == 0.0f && controller.Axes[3] == 0.0f &&
			(controller.Axes[0] != 0.0f || controller.Axes[1] != 0.0f);
		const size_t sourceAxis = useGenericPair ? 0 : 2;
		WebXRInput.SynthesizedAxes[output] = controller.Axes[sourceAxis];
		WebXRInput.SynthesizedAxes[output + 1] = controller.Axes[sourceAxis + 1];

		if (slot == 0)
			nextAxesActive |= 3u << output;
		else if (WebXRTurnModeSetting == WebXRTurnMode::Binding)
		{
			// Explicit legacy/binding mode releases comfort turning and routes
			// the right stick through JoyU/JoyV exactly like the left stick.
			nextAxesActive |= 3u << output;
			InputAxisEvent(AxisKeys[output], WebXRInput.SynthesizedAxes[output] * WebXRJoystickAxisScale);
			InputAxisEvent(AxisKeys[output + 1], WebXRInput.SynthesizedAxes[output + 1] * WebXRJoystickAxisScale);
		}
	}

	bool headTracked = snapshot.HeadPoseValid;
	const vec3 headForward(snapshot.HeadPose.LocalForward[0],
		snapshot.HeadPose.LocalForward[1], snapshot.HeadPose.LocalForward[2]);
	bool handTracked = false;
	vec3 handForward(1.0f, 0.0f, 0.0f);
	if (WebXRInput.DominantControllerIndex >= 0)
	{
		const WebXRControllerState& controller =
			snapshot.Controllers[WebXRInput.DominantControllerIndex];
		const WebXRInputPose* pose = nullptr;
		if (controller.Flags & WebXRAimValid)
			pose = &controller.AimPose;
		else if (controller.Flags & WebXRGripValid)
			pose = &controller.GripPose;
		if (pose)
		{
			handTracked = true;
			handForward = vec3(
				pose->LocalForward[0], pose->LocalForward[1], pose->LocalForward[2]);
		}
	}
	bool movementFellBack = false;
	const vec3 movementReferenceForward = ResolveWebXRMovementForward(
		WebXRMovementReferenceSetting, headTracked, headForward, handTracked, handForward,
		WebXRLocomotion.LastMovementReferenceUsed, movementFellBack);
	if (movementFellBack)
		WebXRLocomotion.MovementFallbackCount++;
	const WebXRMovementAxes movementAxes = TransformWebXRMovementAxes(
		WebXRInput.SynthesizedAxes[0], WebXRInput.SynthesizedAxes[1], movementReferenceForward);

	// The left stick remains in the ordinary, remappable JoyX/JoyY path. Only
	// its horizontal reference is changed; normalized values are still expanded
	// for stock Speed=2 bindings to preserve the established 7000 scale.
	if (WebXRInput.Controllers[0].Connected)
	{
		InputAxisEvent(IK_JoyX, movementAxes.Strafe * WebXRJoystickAxisScale);
		InputAxisEvent(IK_JoyY, movementAxes.Forward * WebXRJoystickAxisScale);
	}
	WebXRLocomotion.LastMoveStrafe = movementAxes.Strafe * WebXRUE1MovementScale;
	WebXRLocomotion.LastMoveForward = movementAxes.Forward * WebXRUE1MovementScale;
	WebXRLocomotion.LastTurnDelta = 0;
	const bool rightControllerConnected = WebXRInput.Controllers[1].Connected;
	if (!rightControllerConnected)
	{
		WebXRSnapTurnArmed = true;
	}
	else if (WebXRTurnModeSetting != WebXRTurnMode::Binding)
	{
		const int turnDelta = ComputeWebXRTurnDelta(WebXRInput.SynthesizedAxes[2], timeElapsed,
			WebXRTurnModeSetting, WebXRSnapTurnDegrees, WebXRSmoothTurnDegreesPerSecond,
			WebXRSnapTurnThreshold, WebXRSnapTurnRearmThreshold,
			WebXRSnapTurnArmed, WebXRLocomotion.SnapTurnCount);
		WebXRLocomotion.LastTurnDelta = turnDelta;

		// Turn only the body/view yaw before the simulation tick. Headset pose
		// subsequently composes on that authoritative yaw in the WebXR bridge.
		// Pitch, roll, location, physics, and collision state are untouched.
		if (turnDelta != 0 && viewport && viewport->Actor())
		{
			UActor* body = viewport->Actor();
			body->Rotation().Yaw = (body->Rotation().Yaw + turnDelta) & 0xffff;
			if (UPawn* pawn = UObject::TryCast<UPawn>(body))
				pawn->ViewRotation().Yaw = (pawn->ViewRotation().Yaw + turnDelta) & 0xffff;
		}
	}

	// Compose gameplay-ready hand poses only after comfort turning has updated
	// the pawn/body yaw, but still inside UpdateInput and therefore before any
	// Tick/Fire script runs. The bridge prepared these local poses from the same
	// recenter origin as this frame's eyes before AdvanceGameFrame began.
	int bodyYaw = CameraRotation.Yaw;
	if (viewport && viewport->Actor())
	{
		if (UPawn* pawn = UObject::TryCast<UPawn>(viewport->Actor()))
			bodyYaw = pawn->ViewRotation().Yaw;
		else
			bodyYaw = viewport->Actor()->Rotation().Yaw;
	}
	const Coords bodyRotation = Coords::Rotation(Rotator(0, bodyYaw, 0));
	// On the first XR-owned frame PlayerCalcView has not necessarily run yet.
	// Anchor hands to the live actor instead of the zero-initialized camera in
	// that case; subsequent frames use the authoritative scripted camera.
	vec3 cameraAnchor = CameraLocation;
	if (!HasCalculatedCameraView && viewport && viewport->Actor())
		cameraAnchor = viewport->Actor()->Location();
	if (snapshot.HeadPoseValid)
		ComposeWebXRWorldPose(WebXRInput.HeadPose, snapshot.HeadPose, cameraAnchor, bodyRotation);
	for (size_t slot = 0; slot < WebXRInput.Controllers.size(); slot++)
	{
		VRControllerInputState& controller = WebXRInput.Controllers[slot];
		const WebXRControllerState& source = snapshot.Controllers[slot];
		ComposeWebXRWorldPose(controller.GripPose, source.GripPose, cameraAnchor, bodyRotation);
		ComposeWebXRWorldPose(controller.AimPose, source.AimPose, cameraAnchor, bodyRotation);
	}

	const uint32_t releasedAxes = WebXRAxesActive & ~nextAxesActive;
	for (uint32_t axis = 0; axis < 4; axis++)
	{
		if ((releasedAxes & (1u << axis)) != 0)
			InputEvent(AxisKeys[axis], IST_Release);
	}
	WebXRAxesActive = nextAxesActive;
#endif
}

void Engine::OpenWindow()
{
	if (!window)
		window = GameWindow::Create(this);

	int width = client->StartupFullscreen ? client->FullscreenViewportX : client->WindowedViewportX;
	int height = client->StartupFullscreen ? client->FullscreenViewportY : client->WindowedViewportY;
#ifdef __EMSCRIPTEN__
	// Never request the browser Fullscreen API, even if StartupFullscreen=True
	// in the ini (UT99's shipped default). Two reasons: (1) it requires a user
	// gesture, which --autoplay boot never has, so the request itself is a
	// no-op/rejected promise; (2) it's the trigger for a real dangling-pointer
	// bug in Emscripten's bundled SDL2 port - Emscripten_SetWindowFullscreen()
	// (SDL_emscriptenvideo.c) hands the SDL_WindowData* to libhtml5.js's
	// registerRestoreOldStyle(), which installs a *document*-level
	// 'fullscreenchange' listener (restoreOldStyle) that SDL's own
	// Emscripten_UnregisterEventHandlers() doesn't know about and never
	// removes. If that listener fires after SDL_DestroyWindow() has already
	// freed window->driverdata, restoreOldStyle() calls back into
	// Emscripten_HandleCanvasResize() with the freed pointer, which reads the
	// now-garbage canvas_id field and passes it to
	// emscripten_get_element_css_size() -> findEventTarget() ->
	// document.querySelector() with a garbage string, throwing an uncaught
	// SyntaxError. Confirmed via a -sSAFE_HEAP=1 -g2 scratch build; see
	// WEBXR_IMPLEMENTATION_PLAN.md. Once WebXR session presentation (M4)
	// exists, that's the real "fullscreen" equivalent for this build anyway.
	bool fullscreen = false;
#else
	bool fullscreen = client->StartupFullscreen;
#endif

	std::string versionString = !LaunchInfo.gameVersionString.empty() ? " (v" + LaunchInfo.gameVersionString + ")" : "";

	window->SetWindowTitle(LaunchInfo.gameName + versionString + " - Surreal Engine");
	window->SetFrameGeometry(Rect::xywh(0.0, 0.0, width, height));
	viewport->SetViewportRect(0, 0, width, height);

	if (fullscreen)
		window->ShowFullscreen();
	else
		window->ShowNormal();
}

void Engine::CloseWindow()
{
	window.reset();
}

void Engine::TickWindow()
{
	if (window && engine->LaunchInfo.ue1Version > 219)
	{
		if (viewport->bShowWindowsMouse() && viewport->bWindowsMouseAvailable())
			window->UnlockCursor();
		else
			window->LockCursor();
	}

	InputEvent(IK_MouseX, IST_Axis, 0);
	InputEvent(IK_MouseY, IST_Axis, 0);

	GameWindow::ProcessEvents();

	if (MouseMoveX != 0 || MouseMoveY != 0)
	{
		int dx = MouseMoveX;
		int dy = MouseMoveY;
		MouseMoveX = 0;
		MouseMoveY = 0;

		// Send to input subsystem.
		if (dx)
			InputEvent(IK_MouseX, IST_Axis, dx);
		if (dy)
			InputEvent(IK_MouseY, IST_Axis, -dy);
	}
}

void Engine::OnWindowPaint()
{
}

void Engine::OnWindowMouseMove(const Point& pos)
{
	if (playingAvi)
		return;

	if (engine->dxRootWindow && engine->dxRootWindow->OnWindowMouseMove(pos))
		return;

	if (engine->LaunchInfo.ue1Version > 219)
	{
		viewport->WindowsMouseX() = (float)(pos.x * window->GetDpiScale());
		viewport->WindowsMouseY() = (float)(pos.y * window->GetDpiScale());
	}
}

void Engine::OnWindowMouseDown(const Point& pos, EInputKey key)
{
	if (playingAvi)
		return;

	if (engine->dxRootWindow && engine->dxRootWindow->OnWindowMouseDown(pos, key))
		return;

	InputEvent(key, IST_Press);
}

void Engine::OnWindowMouseDoubleclick(const Point& pos, EInputKey key)
{
	if (engine->dxRootWindow && engine->dxRootWindow->OnWindowMouseDoubleclick(pos, key))
		return;
}

void Engine::OnWindowMouseUp(const Point& pos, EInputKey key)
{
	if (playingAvi)
		return;

	if (engine->dxRootWindow && engine->dxRootWindow->OnWindowMouseUp(pos, key))
		return;

	InputEvent(key, IST_Release);
}

void Engine::OnWindowMouseWheel(const Point& pos, EInputKey key)
{
	if (playingAvi)
		return;

	if (engine->dxRootWindow && engine->dxRootWindow->OnWindowMouseWheel(pos, key))
		return;

	InputEvent(key, IST_Press);
	InputEvent(key, IST_Release);
}

void Engine::OnWindowRawMouseMove(int dx, int dy)
{
	if (playingAvi)
		return;

	if (engine->dxRootWindow && engine->dxRootWindow->OnWindowRawMouseMove(dx, dy))
		return;

	MouseMoveX += dx;
	MouseMoveY += dy;
}

void Engine::OnWindowKeyChar(std::string chars)
{
	if (playingAvi)
		return;

	if (engine->dxRootWindow && engine->dxRootWindow->OnWindowKeyChar(chars))
		return;

	Key(chars);
}

void Engine::OnWindowKeyDown(EInputKey key)
{
	if (playingAvi)
	{
		if (key == EInputKey::IK_Escape)
			skipAvi = true;
		return;
	}

	if (engine->dxRootWindow && engine->dxRootWindow->OnWindowKeyDown(key))
		return;

	InputEvent(key, IST_Press);
}

void Engine::OnWindowKeyUp(EInputKey key)
{
	if (playingAvi)
		return;

	if (engine->dxRootWindow && engine->dxRootWindow->OnWindowKeyUp(key))
		return;

	InputEvent(key, IST_Release);
}

void Engine::OnWindowGeometryChanged()
{
}

void Engine::OnWindowClose()
{
	quit = true;
}

void Engine::OnWindowActivated()
{
	//SetPause(false);
}

void Engine::OnWindowDeactivated()
{
	//SetPause(true);
}

void Engine::OnWindowDpiScaleChanged()
{
}

void Engine::LockCursor()
{
	if (window)
		window->LockCursor();
}

void Engine::UnlockCursor()
{
	if (window)
		window->UnlockCursor();
}

void Engine::Key(std::string key)
{
	if (Frame::RunState != FrameRunState::Running || playingAvi)
		return;

	if (LaunchInfo.IsDeusEx() && key == "~" && window->GetKeyState(IK_Shift))
	{
		// Did they REALLY hack UE1 to do something as lame as this??
		console->GotoState("Typing", {});
	}

	for (char c : key)
	{
		CallEvent(console, EventName::KeyType, { ExpressionValue::ByteValue(c) });
	}
}

void Engine::InputEvent(EInputKey key, EInputType type, int delta)
{
	if (Frame::RunState != FrameRunState::Running || playingAvi)
		return;

	bool handled = CallEvent(console, EventName::KeyEvent, { ExpressionValue::ByteValue(key), ExpressionValue::ByteValue(type), ExpressionValue::FloatValue((float)delta) }).ToBool();
	
	if (!handled)
	{
		if ((type == EInputType::IST_Press || type == EInputType::IST_Axis) && key >= 0 && key < 256)
		{
			if (type == EInputType::IST_Press)
				delta = 20;
			else
				delta *= 16;

			for (const std::string& command : GetSubcommands(keybindings[keynames[key]]))
			{
				auto it = inputAliases.find(command);
				if (it != inputAliases.end())
				{
					InputCommand(it->second, key, delta);
				}
				else
				{
					InputCommand(command, key, delta);
				}
			}
		}
		else if (type == EInputType::IST_Release)
		{
			for (auto it = activeInputButtons.begin(); it != activeInputButtons.end();)
			{
				if (it->second == key)
				{
					viewport->Actor()->SetBool(it->first, false);
					it = activeInputButtons.erase(it);
				}
				else
				{
					++it;
				}
			}

			for (auto it = activeInputAxes.begin(); it != activeInputAxes.end();)
			{
				if (it->second.Key == key)
				{
					viewport->Actor()->SetFloat(it->first, 0.0f);
					it = activeInputAxes.erase(it);
				}
				else
				{
					++it;
				}
			}
		}
	}
}

void Engine::InputAxisEvent(EInputKey key, float delta)
{
	if (Frame::RunState != FrameRunState::Running || playingAvi)
		return;

	bool handled = CallEvent(console, EventName::KeyEvent, {
		ExpressionValue::ByteValue(key),
		ExpressionValue::ByteValue(EInputType::IST_Axis),
		ExpressionValue::FloatValue(delta)
	}).ToBool();
	if (handled || key < 0 || key >= 256)
		return;

	for (const std::string& command : GetSubcommands(keybindings[keynames[key]]))
	{
		auto it = inputAliases.find(command);
		if (it != inputAliases.end())
			InputCommand(it->second, key, delta);
		else
			InputCommand(command, key, delta);
	}
}

bool Engine::ExecCommand(const Array<std::string>& args)
{
	for (UObject* target : { static_cast<UObject*>(viewport->Actor()), static_cast<UObject*>(console) })
	{
		if (!target)
			continue;

		UFunction* func = FindEventFunction(target, args[0]);
		if (func && AllFlags(func->FuncFlags, FunctionFlags::Exec))
		{
			Array<ExpressionValue> vmArgs;
			int argindex = 0;
			for (UField* field = func->Children; field != nullptr; field = field->Next)
			{
				UProperty* prop = UObject::TryCast<UProperty>(field);
				if (!prop)
					continue;

				if (AllFlags(prop->PropFlags, PropertyFlags::ReturnParm))
					continue;

				if (!AllFlags(prop->PropFlags, PropertyFlags::Parm))
					continue;

				if (argindex + 1 < args.size())
				{
					const std::string& arg = args[1 + argindex];
					switch (prop->ValueType)
					{
					case ExpressionValueType::Nothing: vmArgs.push_back(ExpressionValue::NothingValue()); break;
					case ExpressionValueType::ValueByte: vmArgs.push_back(ExpressionValue::ByteValue(std::atoi(arg.c_str()))); break;
					case ExpressionValueType::ValueInt: vmArgs.push_back(ExpressionValue::IntValue(std::atoi(arg.c_str()))); break;
					case ExpressionValueType::ValueBool: vmArgs.push_back(ExpressionValue::BoolValue(arg == "1" || arg == "true")); break;
					case ExpressionValueType::ValueFloat: vmArgs.push_back(ExpressionValue::FloatValue((float)std::atof(arg.c_str()))); break;
					case ExpressionValueType::ValueString: vmArgs.push_back(ExpressionValue::StringValue(arg)); break;
					case ExpressionValueType::ValueName: vmArgs.push_back(ExpressionValue::NameValue(arg)); break;
					default: LogMessage("Unsupported value type found in Engine.ExecCommand"); return false;
					}
				}
				else if (AllFlags(prop->PropFlags, PropertyFlags::OptionalParm))
				{
					vmArgs.push_back(ExpressionValue::NothingValue());
				}
				else
				{
					switch (prop->ValueType)
					{
					case ExpressionValueType::Nothing: vmArgs.push_back(ExpressionValue::NothingValue()); break;
					case ExpressionValueType::ValueByte: vmArgs.push_back(ExpressionValue::ByteValue(0)); break;
					case ExpressionValueType::ValueInt: vmArgs.push_back(ExpressionValue::IntValue(0)); break;
					case ExpressionValueType::ValueBool: vmArgs.push_back(ExpressionValue::BoolValue(false)); break;
					case ExpressionValueType::ValueFloat: vmArgs.push_back(ExpressionValue::FloatValue(0.0f)); break;
					case ExpressionValueType::ValueString: vmArgs.push_back(ExpressionValue::StringValue({})); break;
					case ExpressionValueType::ValueName: vmArgs.push_back(ExpressionValue::NameValue({})); break;
					default: LogMessage("Unsupported value type found in Engine.ExecCommand"); return false;
					}
				}
				argindex++;
			}

			CallEvent(target, func->Name, vmArgs);
			return true;
		}
	}

	return false;
}

void Engine::InputCommand(const std::string& commands, EInputKey key, float delta)
{
	for (const std::string& commandline : GetSubcommands(commands))
	{
		Array<std::string> args = GetArgs(commandline);
		if (!args.empty())
		{
			std::string command = args[0];
			for (char& c : command) c = std::tolower(c);

			if (command == "button" && args.size() == 2)
			{
				activeInputButtons[args[1]] = key;
			}
			else if (command == "axis" && args.size() == 3)
			{
				float speed = 1.0f;
				if (args[2].size() > 6 && args[2].substr(0, 6) == "Speed=")
					speed = (float)std::atof(args[2].substr(6).c_str());
				activeInputAxes[args[1]] = { speed * delta, key };
			}
			else
			{
				ExecCommand(args);
			}
		}
	}
}

void Engine::SetPause(bool value)
{
	m_GamePaused = value;
}

void Engine::LogGamePackageSHA1Sums() const
{
	auto systemPath = packages->GetSystemFolderPath();

	LogMessage("SHA1Sums of some system files:");
	LogMessage("Core.u: " + SHA1Sum::of_file(systemPath / "Core.u"));
	LogMessage("Engine.u: " + SHA1Sum::of_file(systemPath / "Engine.u"));

	if (packages->IsUnrealTournament())
	{
		LogMessage("Botpack.u: " + SHA1Sum::of_file(systemPath / "Botpack.u"));
		LogMessage("UnrealI.u: " + SHA1Sum::of_file(systemPath / "UnrealI.u"));
		LogMessage("UnrealShare.u: " + SHA1Sum::of_file(systemPath / "UnrealShare.u"));
	}
	else if (packages->IsUnreal1())
	{
		LogMessage("UnrealI.u: " + SHA1Sum::of_file(systemPath / "UnrealI.u"));
		LogMessage("UnrealShare.u: " + SHA1Sum::of_file(systemPath / "UnrealShare.u"));
	}
	else if (packages->IsKlingonHonorGuard())
	{
		LogMessage("Klingons.u: " + SHA1Sum::of_file(systemPath / "Klingons.u"));
	}
}

void Engine::GetLevelInfoObject()
{
	LevelInfo = UObject::Cast<ULevelInfo>(LevelPackage->GetUObject("LevelInfo", "LevelInfo0"));
	if (LaunchInfo.ue1Version < 300) // Unknown when this changed
	{
		for (int grr = 1; !LevelInfo && grr < 20; grr++)
			LevelInfo = UObject::Cast<ULevelInfo>(LevelPackage->GetUObject("LevelInfo", "LevelInfo" + std::to_string(grr)));
	}
	if (!LevelInfo)
		Exception::Throw("Could not find the LevelInfo object for " + LevelPackage->GetPackageName().ToString() + "!");
}

void Engine::GetLevelObject()
{
	Level = UObject::Cast<ULevel>(LevelPackage->GetUObject("Level", "MyLevel"));
	if (!Level)
		Exception::Throw("Could not find the Level object for" + LevelPackage->GetPackageName().ToString() + "!");

	if (LaunchInfo.IsDeusEx())
	{
		// Also try to find DeusExLevelInfo
		DeusExLevelInfo = UObject::Cast<UDeusExLevelInfo>(LevelPackage->GetUObject("DeusExLevelInfo", "DeusExLevelInfo0"));

		// Didn't find it. Keep searching.
		for (int grr = 1; !DeusExLevelInfo && grr < 20; grr++)
			DeusExLevelInfo = UObject::Cast<UDeusExLevelInfo>(LevelPackage->GetUObject("DeusExLevelInfo", "DeusExLevelInfo" + std::to_string(grr)));

		// Entry.dx does not have a DeusExLevelInfo
		/*
		if (!DeusExLevelInfo)
			Exception::Throw("Could not find the DeusExLevelInfo object for " + url.Map + "!");
		*/

		// Link giveObject for all events in the mission
		if (UConversationList* conList = GetDeusExMission())
		{
			for (UConItem* item = conList->conversations(); item; item = item->Next())
			{
				auto conversation = UObject::Cast<UConversation>(item->ConObject());
				for (UConEvent* e = conversation->eventList(); e; e = e->nextEvent())
				{
					EEventType eventType = (EEventType)e->eventType();
					if (eventType == EEventType::TransferObject)
					{
						if (auto transfer = UObject::Cast<UConEventTransferObject>(e))
						{
							UClass* cls = engine->packages->FindClass("DeusEx." + transfer->ObjectName());
							if (!cls)
								LogMessage("Could not find class for TransferObject: " + transfer->ObjectName());
							transfer->giveObject() = cls;
						}
					}
					else if (eventType == EEventType::CheckObject)
					{
						if (auto eventCheckObject = UObject::Cast<UConEventCheckObject>(e))
						{
							if (eventCheckObject->ObjectName().starts_with("NK_"))
							{
								eventCheckObject->checkObject() = nullptr;
							}
							else
							{
								UClass* cls = engine->packages->FindClass("DeusEx." + eventCheckObject->ObjectName());
								if (!cls)
									LogMessage("Could not find class for CheckObject: " + eventCheckObject->ObjectName());
								eventCheckObject->checkObject() = cls;
							}
						}
					}
				}

				// Remove comments from event lists:
				while (conversation->eventList() && (EEventType)conversation->eventList()->eventType() == EEventType::Comment)
					conversation->eventList() = conversation->eventList()->nextEvent();
				UConEvent* cur = conversation->eventList();
				while (cur != nullptr)
				{
					auto next = cur->nextEvent();
					if (next && (EEventType)next->eventType() == EEventType::Comment)
					{
						cur->nextEvent() = next->nextEvent();
					}
					else
					{
						cur = next;
					}
				}
			}
		}
	}
}

void Engine::LinkActorsToLevel()
{
	// Link actors to the level
	for (UActor* actor : Level->Actors)
	{
		if (actor)
		{
			actor->XLevel() = Level;
			Level->Collision.AddToCollision(actor);
			Level->Light.AddLight(actor);
		}
	}
}

const char* Engine::keynames[256] =
{
	/*00*/ "None", "LeftMouse", "RightMouse", "Cancel",
	/*04*/ "MiddleMouse", "Unknown05", "Unknown06", "Unknown07",
	/*08*/ "Backspace", "Tab", "Unknown0A", "Unknown0B",
	/*0C*/ "Unknown0C", "Enter", "Unknown0E", "Unknown0F",
	/*10*/ "Shift", "Ctrl", "Alt", "Pause",
	/*14*/ "CapsLock", "Unknown15", "Unknown16", "Unknown17",
	/*18*/ "Unknown18", "Unknown19", "Unknown1A", "Escape",
	/*1C*/ "Unknown1C", "Unknown1D", "Unknown1E", "Unknown1F",
	/*20*/ "Space", "PageUp", "PageDown", "End",
	/*24*/ "Home", "Left", "Up", "Right",
	/*28*/ "Down", "Select", "Print", "Execute",
	/*2C*/ "PrintScrn", "Insert", "Delete", "Help",
	/*30*/ "0", "1", "2", "3",
	/*34*/ "4", "5", "6", "7",
	/*38*/ "8", "9", "Unknown3A", "Unknown3B",
	/*3C*/ "Unknown3C", "Unknown3D", "Unknown3E", "Unknown3F",
	/*40*/ "Unknown40", "A", "B", "C",
	/*44*/ "D", "E", "F", "G",
	/*48*/ "H", "I", "J", "K",
	/*4C*/ "L", "M", "N", "O",
	/*50*/ "P", "Q", "R", "S",
	/*54*/ "T", "U", "V", "W",
	/*58*/ "X", "Y", "Z", "Unknown5B",
	/*5C*/ "Unknown5C", "Unknown5D", "Unknown5E", "Unknown5F",
	/*60*/ "NumPad0", "NumPad1", "NumPad2", "NumPad3",
	/*64*/ "NumPad4", "NumPad5", "NumPad6", "NumPad7",
	/*68*/ "NumPad8", "NumPad9", "GreyStar", "GreyPlus",
	/*6C*/ "Separator", "GreyMinus", "NumPadPeriod", "GreySlash",
	/*70*/ "F1", "F2", "F3", "F4",
	/*74*/ "F5", "F6", "F7", "F8",
	/*78*/ "F9", "F10", "F11", "F12",
	/*7C*/ "F13", "F14", "F15", "F16",
	/*80*/ "F17", "F18", "F19", "F20",
	/*84*/ "F21", "F22", "F23", "F24",
	/*88*/ "Unknown88", "Unknown89", "Unknown8A", "Unknown8B",
	/*8C*/ "Unknown8C", "Unknown8D", "Unknown8E", "Unknown8F",
	/*90*/ "NumLock", "ScrollLock", "Unknown92", "Unknown93",
	/*94*/ "Unknown94", "Unknown95", "Unknown96", "Unknown97",
	/*98*/ "Unknown98", "Unknown99", "Unknown9A", "Unknown9B",
	/*9C*/ "Unknown9C", "Unknown9D", "Unknown9E", "Unknown9F",
	/*A0*/ "LShift", "RShift", "LControl", "RControl",
	/*A4*/ "UnknownA4", "UnknownA5", "UnknownA6", "UnknownA7",
	/*A8*/ "UnknownA8", "UnknownA9", "UnknownAA", "UnknownAB",
	/*AC*/ "UnknownAC", "UnknownAD", "UnknownAE", "UnknownAF",
	/*B0*/ "UnknownB0", "UnknownB1", "UnknownB2", "UnknownB3",
	/*B4*/ "UnknownB4", "UnknownB5", "UnknownB6", "UnknownB7",
	/*B8*/ "UnknownB8", "UnknownB9", "Semicolon", "Equals",
	/*BC*/ "Comma", "Minus", "Period", "Slash",
	/*C0*/ "Tilde", "UnknownC1", "UnknownC2", "UnknownC3",
	/*C4*/ "UnknownC4", "UnknownC5", "UnknownC6", "UnknownC7",
	/*C8*/ "Joy1", "Joy2", "Joy3", "Joy4",
	/*CC*/ "Joy5", "Joy6", "Joy7", "Joy8",
	/*D0*/ "Joy9", "Joy10", "Joy11", "Joy12",
	/*D4*/ "Joy13", "Joy14", "Joy15", "Joy16",
	/*D8*/ "UnknownD8", "UnknownD9", "UnknownDA", "LeftBracket",
	/*DC*/ "Backslash", "RightBracket", "SingleQuote", "UnknownDF",
	/*E0*/ "JoyX", "JoyY", "JoyZ", "JoyR",
	/*E4*/ "MouseX", "MouseY", "MouseZ", "MouseW",
	/*E8*/ "JoyU", "JoyV", "UnknownEA", "UnknownEB",
	/*EC*/ "MouseWheelUp", "MouseWheelDown", "Unknown10E", "Unknown10F",
	/*F0*/ "JoyPovUp", "JoyPovDown", "JoyPovLeft", "JoyPovRight",
	/*F4*/ "UnknownF4", "UnknownF5", "Attn", "CrSel",
	/*F8*/ "ExSel", "ErEof", "Play", "Zoom",
	/*FC*/ "NoName", "PA1", "OEMClear", ""
};
