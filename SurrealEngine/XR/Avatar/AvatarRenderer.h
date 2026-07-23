#pragma once

// M1 scaffolding: draws a UMesh from its auto-rigged bind pose (no IK yet)
// and logs auto-rig diagnostics at map load. See
// Docs/FULLBODY_VR_AVATAR_PLAN.md, milestone M1.

class UActor;
class VisibleFrame;

#include "Math/vec.h"

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

private:
	static bool DiagnosticsEnabledFlag;
};
