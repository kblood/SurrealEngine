#pragma once

#include "Math/rotator.h"
#include "VM/CallHooks.h"

#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace XRWeaponRuntime
{
	enum class AimScopeKind
	{
		None,
		Ballistic,
		TargetAcquisition,
		Presentation
	};

	// Metadata is deliberately detached from UObject. The engine integration is
	// responsible for resolving these names and for accepting only the local
	// player's current, owned weapon before returning a scope request.
	struct CallMetadata
	{
		std::string_view PackageName;
		std::string_view ClassName;
		std::string_view DeclaringStateName;
		std::string_view ActiveStateName;
		std::string_view FunctionName;
	};

	AimScopeKind ClassifyUT99WeaponCall(const CallMetadata& call);

	// Providers calculate these rotations from their own tracked-pose data. A
	// ballistic scope affects only Pawn.ViewRotation; presentation also rotates
	// the weapon so its overlay can follow the controller rather than the head.
	struct TransformInputs
	{
		bool BallisticRotationValid = false;
		Rotator BallisticRotation = Rotator(0, 0, 0);
		bool PresentationRotationValid = false;
		Rotator PresentationRotation = Rotator(0, 0, 0);
	};

	struct RotationTargets
	{
		Rotator* PawnViewRotation = nullptr;
		Rotator* WeaponRotation = nullptr;
	};

	// Returns an empty cleanup when the request is incomplete. Otherwise the
	// returned VM cleanup restores the exact integer Rotators captured at entry.
	// Nested VM calls consequently unwind in normal LIFO order.
	VMCallHookCleanup BeginRotationScope(AimScopeKind kind, RotationTargets targets,
		const TransformInputs& transforms);

	struct ScopeRequest
	{
		CallMetadata Call;
		RotationTargets Targets;
		TransformInputs Transforms;
		bool SuppressDispatch = false;
	};

	enum class PairedOffHandTriggerAction
	{
		DefaultAlternateFire,
		FireSlave,
		ConsumeRelease
	};

	PairedOffHandTriggerAction ResolvePairedOffHandTrigger(bool hasSlave,
		bool pressed, bool alternateFireKeyDown = false);

	using ScopeResolver = std::function<std::optional<ScopeRequest>(UFunction*, UObject*)>;

	// Adapts provider/engine state resolution to the existing VM hook registry.
	// The resolver owns all game-instance, local-player and tracking validation.
	VMCallHook MakeUT99WeaponCallHook(ScopeResolver resolver, int order = 0);
}
