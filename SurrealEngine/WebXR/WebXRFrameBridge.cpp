#include "Precomp.h"
#include "WebXRFrameBridge.h"
#include "WebXRInputState.h"

#include "Engine.h"
#include "Render/RenderSubsystem.h"
#include "Math/quaternion.h"

#include <array>
#include <cmath>
#include <cstring>

namespace
{
	enum FrameError
	{
		FrameOK = 0,
		FrameInvalidHeader = 1,
		FrameInvalidView = 2,
		FrameTextureUnavailable = 3,
		FrameRenderDeviceUnavailable = 4,
		FrameExternalTargetRejected = 5,
		FrameRenderFailed = 6,
		FrameInvalidInput = 7
	};

	int LastFrameError = FrameOK;

	// UT99's default BaseEyeHeight is 64 UU, matching the original engine's
	// roughly one-inch Unreal unit. Keep this mutable because headset testing,
	// accessibility settings, or a differently-scaled game may need another
	// value; all pose conversion goes through this single seam.
	constexpr float DefaultWorldUnitsPerMeter = 1.0f / 0.0254f;
	float WorldUnitsPerMeter = DefaultWorldUnitsPerMeter;

	struct PoseRecenterState
	{
		bool Valid = false;
		uint32_t ResetGeneration = 0;
		uint32_t RecenterCount = 0;
		vec3 OriginMetersUE = vec3(0.0f);
		float YawOffsetUE = 0.0f;
	};

	PoseRecenterState PoseState;

	struct UEPoseAxes
	{
		vec3 Forward;
		vec3 Right;
		vec3 Up;
		vec3 PositionMeters;
	};

	// WebXR is right-handed (+X right, +Y up, -Z forward); UE1 is
	// left-handed (+X forward, +Y right, +Z up). This direct relabeling is
	// deliberately shared by positions and quaternion-derived basis vectors.
	vec3 WebXRVectorToUE1(const vec3& value)
	{
		return vec3(-value.z, value.x, value.y);
	}

	vec3 RotateLocalToWorld(const Coords& rotation, const vec3& value)
	{
		return rotation.XAxis * value.x + rotation.YAxis * value.y + rotation.ZAxis * value.z;
	}

	UEPoseAxes DecodePoseAxes(const float* position, const float* packedOrientation)
	{
		quaternion orientation(packedOrientation[0], packedOrientation[1],
			packedOrientation[2], packedOrientation[3]);
		orientation = normalize(orientation);

		UEPoseAxes result;
		result.Forward = WebXRVectorToUE1(orientation * vec3(0.0f, 0.0f, -1.0f));
		result.Right = WebXRVectorToUE1(orientation * vec3(1.0f, 0.0f, 0.0f));
		result.Up = WebXRVectorToUE1(orientation * vec3(0.0f, 1.0f, 0.0f));
		result.PositionMeters = WebXRVectorToUE1(vec3(
			position[0], position[1], position[2]));
		return result;
	}

	UEPoseAxes DecodePoseAxes(const WebXRFrameABI::View& view)
	{
		return DecodePoseAxes(view.position, view.orientation);
	}

	mat4 DecodeProjection(const WebXRFrameABI::View& view)
	{
		mat4 projection = mat4::from_values(const_cast<float*>(view.projection));
		// WebXR matrices conventionally look down -Z (m[11] < 0), while the
		// existing UE1 render path and its CPU-side view-space tests look down
		// +Z. Reflect view Z at the projection boundary so the exact asymmetric
		// runtime projection is retained. The deterministic M6 diagnostic uses
		// a native left-handed matrix (m[11] > 0), so it needs no reflection.
		if (view.projection[11] < 0.0f)
			projection = projection * mat4::scale(1.0f, 1.0f, -1.0f);
		return projection;
	}

	bool NearlyEqual(float a, float b, float epsilon = 0.0005f)
	{
		return std::abs(a - b) <= epsilon;
	}

	bool IsFiniteArray(const float* values, size_t count)
	{
		for (size_t i = 0; i < count; i++)
		{
			if (!std::isfinite(values[i]))
				return false;
		}
		return true;
	}

	bool IsValidQuaternion(const float* orientation)
	{
		const float lengthSquared =
			orientation[0] * orientation[0] + orientation[1] * orientation[1] +
			orientation[2] * orientation[2] + orientation[3] * orientation[3];
		return std::isfinite(lengthSquared) && lengthSquared >= 0.000001f;
	}

	bool IsInRange(const float* values, size_t count, float minimum, float maximum)
	{
		for (size_t index = 0; index < count; index++)
		{
			if (!std::isfinite(values[index]) || values[index] < minimum || values[index] > maximum)
				return false;
		}
		return true;
	}

	bool DecodeFrame(const void* frameData, uint32_t bufferBytes, WebXRFrameABI::FrameHeader& header,
		std::array<WebXRFrameABI::View, WebXRFrameABI::MaxViews>& views,
		std::array<WebXRFrameABI::Controller, WebXRFrameABI::MaxInputSources>& controllers)
	{
		if (!frameData || bufferBytes < sizeof(WebXRFrameABI::FrameHeader))
		{
			LastFrameError = FrameInvalidHeader;
			return false;
		}

		std::memcpy(&header, frameData, sizeof(header));
		if (header.version != WebXRFrameABI::Version || header.flags != WebXRFrameABI::KnownFlags ||
			header.viewCount == 0 || header.viewCount > WebXRFrameABI::MaxViews ||
			header.inputSourceCount > WebXRFrameABI::MaxInputSources ||
			header.byteSize != bufferBytes ||
			header.inputOffset != sizeof(header) + header.viewCount * sizeof(WebXRFrameABI::View) ||
			header.byteSize != header.inputOffset + header.inputSourceCount * sizeof(WebXRFrameABI::Controller) ||
			header.byteSize > WebXRFrameABI::MaxPacketBytes ||
			header.textureWidth == 0 || header.textureHeight == 0 || !std::isfinite(header.timestamp))
		{
			LastFrameError = FrameInvalidHeader;
			return false;
		}

		const uint8_t* viewBytes = static_cast<const uint8_t*>(frameData) + sizeof(header);
		for (uint32_t index = 0; index < header.viewCount; index++)
		{
			WebXRFrameABI::View& view = views[index];
			std::memcpy(&view, viewBytes + index * sizeof(view), sizeof(view));

			if (view.eye > WebXRFrameABI::Right || view.viewportX < 0 || view.viewportY < 0 ||
				view.viewportWidth <= 0 || view.viewportHeight <= 0 ||
				static_cast<uint32_t>(view.viewportWidth) > header.textureWidth ||
				static_cast<uint32_t>(view.viewportHeight) > header.textureHeight ||
				static_cast<uint32_t>(view.viewportX) > header.textureWidth - static_cast<uint32_t>(view.viewportWidth) ||
				static_cast<uint32_t>(view.viewportY) > header.textureHeight - static_cast<uint32_t>(view.viewportHeight) ||
				!IsFiniteArray(view.position, 3) || !IsFiniteArray(view.orientation, 4) ||
				!IsFiniteArray(view.projection, 16))
			{
				LastFrameError = FrameInvalidView;
				return false;
			}

			if (!IsValidQuaternion(view.orientation))
			{
				LastFrameError = FrameInvalidView;
				return false;
			}
		}

		const uint8_t* inputBytes = static_cast<const uint8_t*>(frameData) + header.inputOffset;
		for (uint32_t index = 0; index < header.inputSourceCount; index++)
		{
			WebXRFrameABI::Controller& controller = controllers[index];
			std::memcpy(&controller, inputBytes + index * sizeof(controller), sizeof(controller));
			const bool connected = (controller.flags & WebXRConnected) != 0;
			if (controller.handedness > WebXRHandRight ||
				(controller.flags & ~WebXRFrameABI::KnownControllerFlags) != 0 ||
				controller.reserved != 0 ||
				(controller.buttonsPressed & ~0xffffu) != 0 ||
				(controller.buttonsTouched & ~0xffffu) != 0 ||
				!IsInRange(controller.axes, 4, -1.0f, 1.0f) ||
				!IsInRange(controller.buttonValues, 8, 0.0f, 1.0f) ||
				!IsFiniteArray(controller.gripPosition, 3) ||
				!IsFiniteArray(controller.gripOrientation, 4) ||
				!IsFiniteArray(controller.aimPosition, 3) ||
				!IsFiniteArray(controller.aimOrientation, 4) ||
				((controller.flags & WebXRGripValid) && !IsValidQuaternion(controller.gripOrientation)) ||
				((controller.flags & WebXRAimValid) && !IsValidQuaternion(controller.aimOrientation)))
			{
				LastFrameError = FrameInvalidInput;
				return false;
			}

			// A disconnected record is accepted so the producer can explicitly
			// retire a source; publishing normalizes it to a completely empty slot.
			// Connected source IDs must remain unambiguous within the frame.
			if (connected)
			{
				for (uint32_t previous = 0; previous < index; previous++)
				{
					if ((controllers[previous].flags & WebXRConnected) &&
						controllers[previous].sourceId == controller.sourceId)
					{
						LastFrameError = FrameInvalidInput;
						return false;
					}
				}
			}
		}

		LastFrameError = FrameOK;
		return true;
	}

	void PreparePoseRecenter(const WebXRFrameABI::FrameHeader& header,
		const std::array<WebXRFrameABI::View, WebXRFrameABI::MaxViews>& packedViews,
		PoseRecenterState& poseState)
	{
		if (poseState.Valid && poseState.ResetGeneration == header.resetGeneration)
			return;

		vec3 centerMetersUE(0.0f);
		vec3 averageForwardUE(0.0f);
		for (uint32_t index = 0; index < header.viewCount; index++)
		{
			const UEPoseAxes axes = DecodePoseAxes(packedViews[index]);
			centerMetersUE += axes.PositionMeters;
			averageForwardUE += axes.Forward;
		}
		centerMetersUE /= static_cast<float>(header.viewCount);
		averageForwardUE = normalize(averageForwardUE);

		// Recenter around the runtime-provided eye center, preserving per-eye
		// separation. Only yaw is neutralized; pitch and roll remain tracking.
		const float horizontalLength = std::sqrt(averageForwardUE.x * averageForwardUE.x +
			averageForwardUE.y * averageForwardUE.y);
		const float rawYaw = horizontalLength > 0.0001f ?
			std::atan2(-averageForwardUE.y, averageForwardUE.x) : 0.0f;
		poseState.OriginMetersUE = centerMetersUE;
		poseState.YawOffsetUE = -rawYaw;
		poseState.ResetGeneration = header.resetGeneration;
		poseState.Valid = true;
		poseState.RecenterCount++;
	}

	void ComposeLocalControllerPose(const WebXRInputPose& source, float worldUnitsPerMeter,
		const PoseRecenterState& poseState, WebXRInputPose& target)
	{
		const UEPoseAxes axes = DecodePoseAxes(source.Position, source.Orientation);
		const Coords recenterRotation = Coords::YawRotation(poseState.YawOffsetUE);
		const vec3 position = RotateLocalToWorld(recenterRotation,
			(axes.PositionMeters - poseState.OriginMetersUE) * worldUnitsPerMeter);
		const vec3 forward = normalize(RotateLocalToWorld(recenterRotation, axes.Forward));
		vec3 right = normalize(RotateLocalToWorld(recenterRotation, axes.Right));
		vec3 up = normalize(cross(forward, right));
		const vec3 expectedUp = normalize(RotateLocalToWorld(recenterRotation, axes.Up));
		if (dot(up, expectedUp) < 0.0f)
		{
			right = -right;
			up = -up;
		}
		target.LocalPositionUU[0] = position.x;
		target.LocalPositionUU[1] = position.y;
		target.LocalPositionUU[2] = position.z;
		target.LocalForward[0] = forward.x;
		target.LocalForward[1] = forward.y;
		target.LocalForward[2] = forward.z;
		target.LocalRight[0] = right.x;
		target.LocalRight[1] = right.y;
		target.LocalRight[2] = right.z;
		target.LocalUp[0] = up.x;
		target.LocalUp[1] = up.y;
		target.LocalUp[2] = up.z;
	}

	void BuildInputSnapshot(const WebXRFrameABI::FrameHeader& header,
		const std::array<WebXRFrameABI::Controller, WebXRFrameABI::MaxInputSources>& packedControllers,
		float worldUnitsPerMeter, const PoseRecenterState& poseState,
		std::array<WebXRControllerState, WebXRFrameABI::MaxInputSources>& controllers)
	{
		for (uint32_t index = 0; index < header.inputSourceCount; index++)
		{
			const WebXRFrameABI::Controller& source = packedControllers[index];
			WebXRControllerState& target = controllers[index];
			target.SourceId = source.sourceId;
			target.Handedness = source.handedness;
			target.Flags = source.flags;
			target.ButtonsPressed = source.buttonsPressed;
			target.ButtonsTouched = source.buttonsTouched;
			std::memcpy(target.Axes, source.axes, sizeof(target.Axes));
			std::memcpy(target.ButtonValues, source.buttonValues, sizeof(target.ButtonValues));
			std::memcpy(target.GripPose.Position, source.gripPosition, sizeof(target.GripPose.Position));
			std::memcpy(target.GripPose.Orientation, source.gripOrientation, sizeof(target.GripPose.Orientation));
			std::memcpy(target.AimPose.Position, source.aimPosition, sizeof(target.AimPose.Position));
			std::memcpy(target.AimPose.Orientation, source.aimOrientation, sizeof(target.AimPose.Orientation));
			if ((target.Flags & WebXRConnected) && (target.Flags & WebXRGripValid))
				ComposeLocalControllerPose(target.GripPose, worldUnitsPerMeter, poseState, target.GripPose);
			if ((target.Flags & WebXRConnected) && (target.Flags & WebXRAimValid))
				ComposeLocalControllerPose(target.AimPose, worldUnitsPerMeter, poseState, target.AimPose);
		}
	}

	WebXRInputPose BuildLocalHeadPose(const WebXRFrameABI::FrameHeader& header,
		const std::array<WebXRFrameABI::View, WebXRFrameABI::MaxViews>& packedViews,
		float worldUnitsPerMeter, const PoseRecenterState& poseState)
	{
		vec3 centerMetersUE(0.0f);
		vec3 averageForwardUE(0.0f);
		vec3 averageRightUE(0.0f);
		vec3 averageUpUE(0.0f);
		for (uint32_t index = 0; index < header.viewCount; index++)
		{
			const UEPoseAxes axes = DecodePoseAxes(packedViews[index]);
			centerMetersUE += axes.PositionMeters;
			averageForwardUE += axes.Forward;
			averageRightUE += axes.Right;
			averageUpUE += axes.Up;
		}
		const float inverseViewCount = 1.0f / static_cast<float>(header.viewCount);
		centerMetersUE *= inverseViewCount;
		averageForwardUE = normalize(averageForwardUE);
		averageRightUE = normalize(averageRightUE);
		averageUpUE = normalize(averageUpUE);

		const Coords recenterRotation = Coords::YawRotation(poseState.YawOffsetUE);
		WebXRInputPose result;
		const vec3 position = RotateLocalToWorld(recenterRotation,
			(centerMetersUE - poseState.OriginMetersUE) * worldUnitsPerMeter);
		const vec3 forward = normalize(RotateLocalToWorld(recenterRotation, averageForwardUE));
		vec3 right = normalize(RotateLocalToWorld(recenterRotation, averageRightUE));
		vec3 up = normalize(cross(forward, right));
		const vec3 expectedUp = normalize(RotateLocalToWorld(recenterRotation, averageUpUE));
		if (dot(up, expectedUp) < 0.0f)
		{
			right = -right;
			up = -up;
		}
		result.LocalPositionUU[0] = position.x;
		result.LocalPositionUU[1] = position.y;
		result.LocalPositionUU[2] = position.z;
		result.LocalForward[0] = forward.x;
		result.LocalForward[1] = forward.y;
		result.LocalForward[2] = forward.z;
		result.LocalRight[0] = right.x;
		result.LocalRight[1] = right.y;
		result.LocalRight[2] = right.z;
		result.LocalUp[0] = up.x;
		result.LocalUp[1] = up.y;
		result.LocalUp[2] = up.z;
		return result;
	}

	void BuildSceneViews(const WebXRFrameABI::FrameHeader& header,
		const std::array<WebXRFrameABI::View, WebXRFrameABI::MaxViews>& packedViews,
		const vec3& cameraLocation, const Coords& bodyRotation, float worldUnitsPerMeter,
		PoseRecenterState& poseState,
		std::array<WebXRSceneView, WebXRFrameABI::MaxViews>& sceneViews)
	{
		std::array<UEPoseAxes, WebXRFrameABI::MaxViews> axes = {};
		for (uint32_t index = 0; index < header.viewCount; index++)
			axes[index] = DecodePoseAxes(packedViews[index]);

		PreparePoseRecenter(header, packedViews, poseState);

		const Coords recenterRotation = Coords::YawRotation(poseState.YawOffsetUE);

		for (uint32_t index = 0; index < header.viewCount; index++)
		{
			const WebXRFrameABI::View& source = packedViews[index];
			WebXRSceneView& target = sceneViews[index];
			target.Eye = source.eye;
			target.ArrayLayer = source.arrayLayer;
			target.ViewportX = source.viewportX;
			target.ViewportY = source.viewportY;
			target.ViewportWidth = source.viewportWidth;
			target.ViewportHeight = source.viewportHeight;

			const vec3 recenteredPosition = RotateLocalToWorld(recenterRotation,
				(axes[index].PositionMeters - poseState.OriginMetersUE) * worldUnitsPerMeter);
			target.Location = cameraLocation + RotateLocalToWorld(bodyRotation, recenteredPosition);

			target.ViewRotation.Origin = vec3(0.0f);
			target.ViewRotation.XAxis = RotateLocalToWorld(bodyRotation,
				RotateLocalToWorld(recenterRotation, axes[index].Forward));
			target.ViewRotation.YAxis = RotateLocalToWorld(bodyRotation,
				RotateLocalToWorld(recenterRotation, axes[index].Right));
			target.ViewRotation.ZAxis = RotateLocalToWorld(bodyRotation,
				RotateLocalToWorld(recenterRotation, axes[index].Up));
			target.WorldToView = Coords::ViewToRenderDev().ToMatrix() *
				target.ViewRotation.Inverse().ToMatrix() * Coords::Location(target.Location).ToMatrix();
			target.Projection = DecodeProjection(source);
		}
	}

	bool RunPoseMathSelfTest()
	{
		WebXRFrameABI::FrameHeader header = {};
		header.version = WebXRFrameABI::Version;
		header.viewCount = 2;
		header.resetGeneration = 7;

		std::array<WebXRFrameABI::View, WebXRFrameABI::MaxViews> views = {};
		for (uint32_t index = 0; index < 2; index++)
		{
			views[index].eye = index + 1;
			views[index].arrayLayer = index;
			views[index].viewportWidth = 640;
			views[index].viewportHeight = 480;
			views[index].position[0] = index == 0 ? -0.032f : 0.032f;
			views[index].orientation[3] = 1.0f;
			views[index].projection[0] = 1.0f;
			views[index].projection[5] = 1.0f;
			views[index].projection[10] = 1.0f;
			views[index].projection[15] = 1.0f;
		}

		const UEPoseAxes identityAxes = DecodePoseAxes(views[0]);
		if (!NearlyEqual(identityAxes.Forward.x, 1.0f) || !NearlyEqual(identityAxes.Forward.y, 0.0f) ||
			!NearlyEqual(identityAxes.Right.y, 1.0f) || !NearlyEqual(identityAxes.Up.z, 1.0f) ||
			!NearlyEqual(WebXRVectorToUE1(vec3(1.0f, 2.0f, 3.0f)).x, -3.0f) ||
			!NearlyEqual(WebXRVectorToUE1(vec3(1.0f, 2.0f, 3.0f)).y, 1.0f) ||
			!NearlyEqual(WebXRVectorToUE1(vec3(1.0f, 2.0f, 3.0f)).z, 2.0f))
			return false;

		// The real WebXR right-handed projection is converted to the engine's
		// +Z view convention exactly once; the M6 left-handed diagnostic is not.
		views[0].projection[11] = -1.0f;
		if (!NearlyEqual(DecodeProjection(views[0])[11], 1.0f))
			return false;
		views[0].projection[11] = 1.0f;
		if (!NearlyEqual(DecodeProjection(views[0])[11], 1.0f))
			return false;
		views[0].projection[11] = 0.0f;

		PoseRecenterState state;
		std::array<WebXRSceneView, WebXRFrameABI::MaxViews> output = {};
		const vec3 anchor(10.0f, 20.0f, 30.0f);
		const Coords body = Coords::Identity();
		BuildSceneViews(header, views, anchor, body, DefaultWorldUnitsPerMeter, state, output);
		const float halfIPDUU = 0.032f * DefaultWorldUnitsPerMeter;
		if (state.RecenterCount != 1 ||
			!NearlyEqual(output[0].Location.x, anchor.x) ||
			!NearlyEqual(output[0].Location.y, anchor.y - halfIPDUU) ||
			!NearlyEqual(output[1].Location.y, anchor.y + halfIPDUU) ||
			!NearlyEqual(output[0].Location.z, anchor.z))
			return false;
		const WebXRInputPose identityHead = BuildLocalHeadPose(
			header, views, DefaultWorldUnitsPerMeter, state);
		if (!NearlyEqual(identityHead.LocalPositionUU[0], 0.0f) ||
			!NearlyEqual(identityHead.LocalPositionUU[1], 0.0f) ||
			!NearlyEqual(identityHead.LocalPositionUU[2], 0.0f) ||
			!NearlyEqual(identityHead.LocalForward[0], 1.0f) ||
			!NearlyEqual(identityHead.LocalRight[1], 1.0f) ||
			!NearlyEqual(identityHead.LocalUp[2], 1.0f))
			return false;

		// Controller poses share that already-captured viewer origin. Identity
		// aim points UE1-forward; WebXR (+X right, -Z forward) position maps to
		// UE1 (+X forward, +Y right), scaled exactly once into world units.
		header.inputSourceCount = 2;
		std::array<WebXRFrameABI::Controller, WebXRFrameABI::MaxInputSources> packedControllers = {};
		packedControllers[0].sourceId = 11;
		packedControllers[0].handedness = WebXRHandRight;
		packedControllers[0].flags = WebXRConnected | WebXRAimValid;
		packedControllers[0].aimPosition[0] = 0.25f;
		packedControllers[0].aimPosition[2] = -0.5f;
		packedControllers[0].aimOrientation[3] = 1.0f;
		// A disconnected record carrying a validity bit must never receive a
		// composed pose, which prevents stale disconnect data reaching gameplay.
		packedControllers[1].flags = WebXRGripValid;
		packedControllers[1].gripPosition[1] = 10.0f;
		packedControllers[1].gripOrientation[3] = 1.0f;
		std::array<WebXRControllerState, WebXRFrameABI::MaxInputSources> controllerStates = {};
		BuildInputSnapshot(header, packedControllers, DefaultWorldUnitsPerMeter, state,
			controllerStates);
		if (!NearlyEqual(controllerStates[0].AimPose.LocalPositionUU[0],
				0.5f * DefaultWorldUnitsPerMeter) ||
			!NearlyEqual(controllerStates[0].AimPose.LocalPositionUU[1],
				0.25f * DefaultWorldUnitsPerMeter) ||
			!NearlyEqual(controllerStates[0].AimPose.LocalPositionUU[2], 0.0f) ||
			!NearlyEqual(controllerStates[0].AimPose.LocalForward[0], 1.0f) ||
			!NearlyEqual(controllerStates[0].AimPose.LocalForward[1], 0.0f) ||
			!NearlyEqual(controllerStates[0].AimPose.LocalRight[1], 1.0f) ||
			!NearlyEqual(controllerStates[0].AimPose.LocalUp[2], 1.0f) ||
			!NearlyEqual(controllerStates[1].GripPose.LocalPositionUU[2], 0.0f))
			return false;

		// Full controller roll survives the WebXR->UE1 handedness conversion.
		// A +90 degree roll around WebXR aim-forward (-Z) maps to UE1 right=-Z,
		// up=+Y while forward remains +X.
		const float controllerHalfQuarterTurn = 0.70710678118f;
		packedControllers[0].aimOrientation[2] = -controllerHalfQuarterTurn;
		packedControllers[0].aimOrientation[3] = controllerHalfQuarterTurn;
		BuildInputSnapshot(header, packedControllers, DefaultWorldUnitsPerMeter, state,
			controllerStates);
		const WebXRInputPose& rolledAim = controllerStates[0].AimPose;
		if (!NearlyEqual(rolledAim.LocalForward[0], 1.0f) ||
			!NearlyEqual(rolledAim.LocalRight[2], -1.0f) ||
			!NearlyEqual(rolledAim.LocalUp[1], 1.0f) ||
			!NearlyEqual(dot(vec3(rolledAim.LocalForward[0], rolledAim.LocalForward[1], rolledAim.LocalForward[2]),
				vec3(rolledAim.LocalRight[0], rolledAim.LocalRight[1], rolledAim.LocalRight[2])), 0.0f))
			return false;
		packedControllers[0].aimOrientation[2] = 0.0f;
		packedControllers[0].aimOrientation[3] = 1.0f;
		header.inputSourceCount = 0;

		// Orientation is transported, not reduced to positional stereo. A
		// quarter-turn about WebXR +Y must rotate the UE forward basis by a
		// quarter-turn while keeping all axes unit length and orthogonal.
		const float halfQuarterTurn = 0.70710678118f;
		views[0].orientation[1] = halfQuarterTurn;
		views[0].orientation[3] = halfQuarterTurn;
		views[1].orientation[1] = halfQuarterTurn;
		views[1].orientation[3] = halfQuarterTurn;
		BuildSceneViews(header, views, anchor, body, DefaultWorldUnitsPerMeter, state, output);
		const WebXRInputPose turnedHead = BuildLocalHeadPose(
			header, views, DefaultWorldUnitsPerMeter, state);
		if (!NearlyEqual(std::abs(output[0].ViewRotation.XAxis.y), 1.0f) ||
			!NearlyEqual(dot(output[0].ViewRotation.XAxis, output[0].ViewRotation.YAxis), 0.0f) ||
			!NearlyEqual(dot(output[0].ViewRotation.XAxis, output[0].ViewRotation.XAxis), 1.0f) ||
			!NearlyEqual(std::abs(turnedHead.LocalForward[1]), 1.0f) ||
			!NearlyEqual(turnedHead.LocalForward[2], 0.0f))
			return false;
		views[0].orientation[1] = 0.0f;
		views[0].orientation[3] = 1.0f;
		views[1].orientation[1] = 0.0f;
		views[1].orientation[3] = 1.0f;

		// Pitch is carried by the headset rather than the body camera. Test the
		// sign-independent cardinal result because handedness is already asserted
		// explicitly by the vector-component checks above.
		views[0].orientation[0] = halfQuarterTurn;
		views[0].orientation[3] = halfQuarterTurn;
		views[1].orientation[0] = halfQuarterTurn;
		views[1].orientation[3] = halfQuarterTurn;
		BuildSceneViews(header, views, anchor, body, DefaultWorldUnitsPerMeter, state, output);
		if (!NearlyEqual(std::abs(output[0].ViewRotation.XAxis.z), 1.0f) ||
			!NearlyEqual(output[0].ViewRotation.XAxis.x, 0.0f))
			return false;
		views[0].orientation[0] = 0.0f;
		views[0].orientation[3] = 1.0f;
		views[1].orientation[0] = 0.0f;
		views[1].orientation[3] = 1.0f;

		// Body yaw composes outside the recentered tracking transform. This
		// keeps locomotion/script camera yaw authoritative without double-applying
		// body pitch or roll.
		PoseRecenterState bodyYawState;
		const Coords quarterTurnBody = Coords::Rotation(Rotator(0, 16384, 0));
		BuildSceneViews(header, views, anchor, quarterTurnBody,
			DefaultWorldUnitsPerMeter, bodyYawState, output);
		if (!NearlyEqual(std::abs(output[0].ViewRotation.XAxis.y), 1.0f) ||
			!NearlyEqual(output[0].ViewRotation.XAxis.x, 0.0f))
			return false;

		// One metre upward after the initial recenter must produce exactly one
		// configured world metre while preserving the runtime-provided IPD.
		views[0].position[1] = 1.0f;
		views[1].position[1] = 1.0f;
		BuildSceneViews(header, views, anchor, body, DefaultWorldUnitsPerMeter, state, output);
		if (state.RecenterCount != 1 ||
			!NearlyEqual(output[0].Location.z, anchor.z + DefaultWorldUnitsPerMeter))
			return false;

		// A reference-space reset captures the new center without swallowing
		// or synthesizing the actual per-eye separation.
		header.resetGeneration++;
		BuildSceneViews(header, views, anchor, body, DefaultWorldUnitsPerMeter, state, output);
		return state.RecenterCount == 2 &&
			NearlyEqual(output[0].Location.z, anchor.z) &&
			NearlyEqual(output[0].Location.y, anchor.y - halfIPDUU) &&
			NearlyEqual(output[1].Location.y, anchor.y + halfIPDUU);
	}
}

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include "RenderDevice/WebGPU/WebGPURenderDevice.h"

EM_JS(uintptr_t, JS_ImportCurrentWebXRFrameTexture, (uintptr_t parentDevice), {
	const texture = window.surrealWebXRFrameTexture;
	if (!texture || typeof WebGPU === "undefined" || !WebGPU.importJsTexture)
		return 0;
	return WebGPU.importJsTexture(texture, parentDevice);
});
#endif

extern "C"
{
	uint32_t Surreal_GetWebXRFrameABIVersion() { return WebXRFrameABI::Version; }
	uint32_t Surreal_GetWebXRFrameHeaderSize() { return sizeof(WebXRFrameABI::FrameHeader); }
	uint32_t Surreal_GetWebXRViewSize() { return sizeof(WebXRFrameABI::View); }
	uint32_t Surreal_GetWebXRFrameMaxViews() { return WebXRFrameABI::MaxViews; }
	uint32_t Surreal_GetWebXRInputRecordSize() { return sizeof(WebXRFrameABI::Controller); }
	uint32_t Surreal_GetWebXRFrameMaxInputs() { return WebXRFrameABI::MaxInputSources; }

	int Surreal_ValidateWebXRFrame(const void* frameData, uint32_t bufferBytes)
	{
		WebXRFrameABI::FrameHeader header = {};
		std::array<WebXRFrameABI::View, WebXRFrameABI::MaxViews> views = {};
		std::array<WebXRFrameABI::Controller, WebXRFrameABI::MaxInputSources> controllers = {};
		return DecodeFrame(frameData, bufferBytes, header, views, controllers) ? 1 : 0;
	}

	int Surreal_GetWebXRFrameLastError() { return LastFrameError; }
	float Surreal_GetWebXRWorldUnitsPerMeter() { return WorldUnitsPerMeter; }

	int Surreal_SetWebXRWorldUnitsPerMeter(float worldUnitsPerMeter)
	{
		if (!std::isfinite(worldUnitsPerMeter) || worldUnitsPerMeter < 1.0f || worldUnitsPerMeter > 10000.0f)
			return 0;
		WorldUnitsPerMeter = worldUnitsPerMeter;
		return 1;
	}

	void Surreal_ResetWebXRPose()
	{
		PoseState.Valid = false;
		ResetWebXRInputState();
	}
	uint32_t Surreal_GetWebXRPoseRecenterCount() { return PoseState.RecenterCount; }
	uint32_t Surreal_GetWebXRPoseResetGeneration() { return PoseState.ResetGeneration; }
	int Surreal_RunWebXRPoseMathSelfTest() { return RunPoseMathSelfTest() ? 1 : 0; }
	uint32_t Surreal_GetWebXRInputFrameGeneration() { return static_cast<uint32_t>(GetLatestWebXRInputSnapshot().FrameGeneration); }
	uint32_t Surreal_GetWebXRPublishedInputSourceCount() { return GetLatestWebXRInputSnapshot().SourceCount; }
	int Surreal_RunWebXRInputStateSelfTest() { return RunWebXRInputStateSelfTest() ? 1 : 0; }

	int Surreal_RenderWebXRFrame(const void* frameData, uint32_t bufferBytes)
	{
		WebXRFrameABI::FrameHeader header = {};
		std::array<WebXRFrameABI::View, WebXRFrameABI::MaxViews> packedViews = {};
		std::array<WebXRFrameABI::Controller, WebXRFrameABI::MaxInputSources> packedControllers = {};
		if (!DecodeFrame(frameData, bufferBytes, header, packedViews, packedControllers))
			return 0;

#ifdef __EMSCRIPTEN__
		if (!engine || !engine->render)
		{
			LastFrameError = FrameRenderDeviceUnavailable;
			return 0;
		}

		auto* device = dynamic_cast<WebGPURenderDevice*>(engine->render->Device);
		if (!device || !device->Context || !device->Context->Device)
		{
			LastFrameError = FrameRenderDeviceUnavailable;
			return 0;
		}

		WGPUTexture texture = reinterpret_cast<WGPUTexture>(
			JS_ImportCurrentWebXRFrameTexture(reinterpret_cast<uintptr_t>(device->Context->Device)));
		if (!texture)
		{
			LastFrameError = FrameTextureUnavailable;
			return 0;
		}

		const WebXRFrameABI::View& firstView = packedViews[0];
		if (!device->BeginExternalRenderTargetFrame(texture,
			static_cast<int>(header.textureWidth), static_cast<int>(header.textureHeight), firstView.arrayLayer))
		{
			wgpuTextureRelease(texture);
			LastFrameError = FrameExternalTargetRejected;
			return 0;
		}
		if (!device->SelectExternalRenderTargetView(firstView.arrayLayer, firstView.viewportX,
			firstView.viewportY, firstView.viewportWidth, firstView.viewportHeight))
		{
			device->EndExternalRenderTargetFrame();
			LastFrameError = FrameExternalTargetRejected;
			return 0;
		}

		const uint64_t tickBefore = engine->tickCount;
		// Capture/reset the viewer-centered origin before UpdateInput. Controller
		// poses published below and stereo views built after PlayerCalcView must
		// use the identical origin during this frame.
		PreparePoseRecenter(header, packedViews, PoseState);
		// Publish a complete replacement before UpdateInput runs inside
		// AdvanceGameFrame. A valid zero-input packet still advances the input
		// generation and clears both slots, producing release edges this tick.
		std::array<WebXRControllerState, WebXRFrameABI::MaxInputSources> controllers = {};
		BuildInputSnapshot(header, packedControllers, WorldUnitsPerMeter, PoseState, controllers);
		const WebXRInputPose headPose = BuildLocalHeadPose(
			header, packedViews, WorldUnitsPerMeter, PoseState);
		PublishWebXRInputSnapshot(controllers.data(), header.inputSourceCount, &headPose);
		const float levelElapsed = engine->AdvanceGameFrame();
		std::array<WebXRSceneView, WebXRFrameABI::MaxViews> sceneViews = {};
		// AdvanceGameFrame runs PlayerCalcView first. Its resulting camera is the
		// authoritative body/locomotion anchor for this same XR render. Only its
		// yaw participates in tracked orientation; headset pose supplies
		// pitch/roll and local translation without mutating pawn/network state.
		const Coords bodyRotation = Coords::Rotation(Rotator(0, engine->CameraRotation.Yaw, 0));
		BuildSceneViews(header, packedViews, engine->CameraLocation, bodyRotation,
			WorldUnitsPerMeter, PoseState, sceneViews);
		const bool rendered = engine->render->DrawGameWebXRViews(
			levelElapsed, sceneViews.data(), header.viewCount);
		engine->FinishGameFrame(levelElapsed);
		const bool ended = device->EndExternalRenderTargetFrame();
		if (!rendered || !ended || engine->tickCount != tickBefore + 1)
		{
			LastFrameError = FrameRenderFailed;
			return 0;
		}

		LastFrameError = FrameOK;
		return 1;
#else
		LastFrameError = FrameRenderDeviceUnavailable;
		return 0;
#endif
	}
}
