#pragma once

// M1 scaffolding: draws a UMesh from its auto-rigged bind pose (no IK yet)
// and logs auto-rig diagnostics at map load. See
// Docs/FULLBODY_VR_AVATAR_PLAN.md, milestone M1.

class UActor;
class UMesh;
class VisibleFrame;

#include "Math/vec.h"
#include "AvatarIKSolver.h"

class AvatarRenderer
{
public:
	// Off by default; set from the --avatar-autorig-debug command line flag.
	// Gates both RunMapLoadDiagnostics and any use of DrawActorBindPose.
	static void SetDiagnosticsEnabled(bool enabled);
	static bool DiagnosticsEnabled();

	// Runs the auto-rig against a handful of distinct Pawn meshes found in
	// the current level (including the local player's), logging joint
	// counts/role labels and a bind-pose reconstruction self-check. No-op if
	// diagnostics are disabled. Never throws.
	static void RunMapLoadDiagnostics();

	// Draws `actor`'s mesh at its auto-rigged bind pose instead of its normal
	// animated pose, offset by `worldOffset` so it can be placed next to the
	// actor's normal render for visual comparison. Returns false (and draws
	// nothing) if the mesh could not be auto-rigged.
	static bool DrawActorBindPose(VisibleFrame* frame, UActor* actor, const vec3& worldOffset);

	// M2: as DrawActorBindPose, but solves arm IK and pelvis/spine
	// extrapolation from `engineInput` (already-converted engine-space head/
	// hand poses - see Engine::GetXRAvatarInput) before skinning, instead of
	// drawing the static bind pose. Returns false (and draws nothing) under
	// the same conditions DrawActorBindPose does.
	static bool DrawActorWithIK(VisibleFrame* frame, UActor* actor, const vec3& worldOffset,
		const AvatarIKFrameInput& engineInput, const AvatarIKOptions& options);

	// Converts already engine-space head/hand poses into the rig's own
	// bind-pose (mesh-local) space using `actor`/`mesh`'s current transform -
	// the same space AvatarJoint::BindOrigin and AvatarSkinner already work
	// in. Exposed mainly so AvatarIKSolverTests-style callers can inspect it;
	// DrawActorWithIK is the normal entry point.
	static AvatarIKInput BuildIKInput(UActor* actor, UMesh* mesh, const AvatarIKFrameInput& engineInput);

	// Headset-free stand-in for a real XR sample, used only behind
	// --avatar-ik-synthetic when no OpenXR session is driving the avatar.
	// Moves the head and both hands deterministically as a function of
	// elapsed time so DrawActorWithIK's effect is visible without a headset.
	static AvatarIKFrameInput BuildSyntheticFrameInput(const vec3& cameraLocation, float timeSeconds);

private:
	static bool DiagnosticsEnabledFlag;
};
