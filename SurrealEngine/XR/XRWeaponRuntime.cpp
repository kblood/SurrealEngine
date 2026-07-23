#include "XRWeaponRuntime.h"

#include <utility>

namespace XRWeaponRuntime
{
	namespace
	{
		bool EqualsIgnoreCase(std::string_view left, std::string_view right)
		{
			if (left.size() != right.size())
				return false;
			for (size_t index = 0; index < left.size(); index++)
			{
				char a = left[index];
				char b = right[index];
				if (a >= 'A' && a <= 'Z') a = static_cast<char>(a - 'A' + 'a');
				if (b >= 'A' && b <= 'Z') b = static_cast<char>(b - 'A' + 'a');
				if (a != b)
					return false;
			}
			return true;
		}

	}

	AimScopeKind ClassifyUT99WeaponCall(const CallMetadata& call)
	{
		// Guided-warhead steering is a separate camera/control policy. Do not
		// implicitly redirect it through the ordinary hand-aim contract.
		if (EqualsIgnoreCase(call.ClassName, "GuidedWarShell"))
			return AimScopeKind::None;

		if (EqualsIgnoreCase(call.FunctionName, "TraceFire") ||
			EqualsIgnoreCase(call.FunctionName, "ProjectileFire") ||
			EqualsIgnoreCase(call.FunctionName, "AdjustAim") ||
			EqualsIgnoreCase(call.FunctionName, "AdjustToss"))
			return AimScopeKind::Ballistic;

		const bool globalFunction = call.DeclaringStateName.empty();
		if (globalFunction && EqualsIgnoreCase(call.FunctionName, "RenderOverlays"))
			return AimScopeKind::Presentation;

		if (!EqualsIgnoreCase(call.PackageName, "Botpack"))
			return AimScopeKind::None;

		auto global = [&](std::string_view className, std::string_view functionName)
		{
			return globalFunction && EqualsIgnoreCase(call.ClassName, className) &&
				EqualsIgnoreCase(call.FunctionName, functionName);
		};
		auto state = [&](std::string_view className, std::string_view stateName,
			std::string_view functionName)
		{
			return EqualsIgnoreCase(call.ClassName, className) &&
				EqualsIgnoreCase(call.DeclaringStateName, stateName) &&
				EqualsIgnoreCase(call.ActiveStateName, stateName) &&
				EqualsIgnoreCase(call.FunctionName, functionName);
		};

		// StarterBolt owns the PulseGun beam after its initial ProjectileFire and
		// reads the instigator's ViewRotation again on every update.
		if (global("UT_FlakCannon", "Fire") || global("UT_FlakCannon", "AltFire") ||
			state("UT_Eightball", "FireRockets", "BeginState") ||
			global("Translocator", "ThrowTarget") || global("ChainSaw", "Slash") ||
			global("ImpactHammer", "TraceAltFire") || state("ImpactHammer", "Firing", "Tick") ||
			global("StarterBolt", "Tick"))
			return AimScopeKind::Ballistic;

		if (global("UT_Eightball", "CheckTarget"))
			return AimScopeKind::TargetAcquisition;

		return AimScopeKind::None;
	}

	VMCallHookCleanup BeginRotationScope(AimScopeKind kind, RotationTargets targets,
		const TransformInputs& transforms)
	{
		if (kind == AimScopeKind::None || !targets.PawnViewRotation)
			return {};

		if (kind == AimScopeKind::Presentation)
		{
			if (!transforms.PresentationRotationValid || !targets.WeaponRotation)
				return {};

			Rotator* const pawn = targets.PawnViewRotation;
			Rotator* const weapon = targets.WeaponRotation;
			const Rotator savedPawn = *pawn;
			const Rotator savedWeapon = *weapon;
			VMCallHookCleanup cleanup = [pawn, weapon, savedPawn, savedWeapon]()
			{
				*pawn = savedPawn;
				*weapon = savedWeapon;
			};
			*pawn = transforms.PresentationRotation;
			*weapon = transforms.PresentationRotation;
			return cleanup;
		}

		if (!transforms.BallisticRotationValid)
			return {};

		Rotator* const pawn = targets.PawnViewRotation;
		const Rotator savedPawn = *pawn;
		VMCallHookCleanup cleanup = [pawn, savedPawn]() { *pawn = savedPawn; };
		*pawn = transforms.BallisticRotation;
		return cleanup;
	}

	VMCallHook MakeUT99WeaponCallHook(ScopeResolver resolver, int order)
	{
		VMCallHook hook;
		hook.Order = order;
		hook.Enter = [resolver = std::move(resolver)](UFunction* function, UObject* instance,
			VMCallArguments& arguments) -> VMCallHookCleanup
		{
			if (!resolver)
				return {};
			std::optional<ScopeRequest> request = resolver(function, instance);
			if (!request)
				return {};
			const AimScopeKind kind = ClassifyUT99WeaponCall(request->Call);
			if (request->SuppressDispatch && kind != AimScopeKind::None)
			{
				arguments.OverrideResult(ExpressionValue::NothingValue());
				return {};
			}
			return BeginRotationScope(kind, request->Targets, request->Transforms);
		};
		return hook;
	}

	PairedOffHandTriggerAction ResolvePairedOffHandTrigger(bool hasSlave,
		bool pressed, bool alternateFireKeyDown)
	{
		if (!hasSlave || (!pressed && alternateFireKeyDown))
			return PairedOffHandTriggerAction::DefaultAlternateFire;
		return pressed ? PairedOffHandTriggerAction::FireSlave :
			PairedOffHandTriggerAction::ConsumeRelease;
	}
}
