#pragma once

// Pure math: rig bind pose + head/hand targets -> per-joint transforms.
// See Docs/FULLBODY_VR_AVATAR_PLAN.md, milestone M2. Everything here operates
// in whichever space the caller's targets are expressed in (this module never
// looks at a UActor/UMesh/XR provider) - AvatarRenderer is the place that
// converts real XR engine-space poses into the rig's own bind-pose (mesh
// local) space before calling Solve, because that is the space
// AvatarJoint::BindOrigin/AvatarSkinner already work in.

#include "AvatarAutoRig.h"
#include "AvatarSkinner.h"
#include "Math/vec.h"
#include "Math/quaternion.h"

// One tracked pose (head or a hand grip), already in the rig's bind-pose
// space. Forward/Up are unit direction vectors (not a full orientation
// frame) - all this solver needs an orientation for is the elbow bend hint
// and the pelvis facing direction, not vertex-accurate hand rotation.
struct AvatarIKTarget
{
	bool Valid = false;
	vec3 Position = vec3(0.0f);
	vec3 Forward = vec3(1.0f, 0.0f, 0.0f);
	vec3 Up = vec3(0.0f, 0.0f, 1.0f);
};

// One foot's ground contact sample, already converted into the rig's
// bind-pose (mesh-local) space by the caller - a straight-down
// CollisionSystem::TraceFirstHit probe from that leg's live hip position
// (see AvatarIKSolver::EstimateLegRoots and AvatarRenderer). Kept as plain
// data here, same reasoning as AvatarIKTarget/AvatarEnginePose above: this
// header never depends on CollisionSystem/UActor.
struct AvatarLegGroundProbe
{
	bool Valid = false; // false if the probe found no ground within range
	vec3 GroundPoint = vec3(0.0f); // mesh-local space; meaningful only if Valid
};

struct AvatarIKInput
{
	AvatarIKTarget Head;
	AvatarIKTarget LeftHand;
	AvatarIKTarget RightHand;
	AvatarLegGroundProbe LeftFootGround;
	AvatarLegGroundProbe RightFootGround;

	// Overall locomotion state: false while airborne/falling/swimming/flying
	// (the caller derives this from the local Pawn's own Physics() state -
	// see AvatarRenderer). Legs blend to a neutral hanging pose instead of
	// ground-probing whenever this is false, regardless of whether a probe
	// still happened to find ground below (plan doc M3).
	bool Grounded = true;
};

struct AvatarIKOptions
{
	// 0 = pelvis/spine stay at bind pose regardless of head motion; 1 = pelvis
	// tracks head motion 1:1 (translated by the bind-pose pelvis-to-head
	// offset). Every VRIK-family implementation surveyed in the plan doc
	// exposes this as a tunable rather than hardcoding full tracking.
	float PelvisFollowHeadWeight = 1.0f;
};

// A pose already in engine-world space (native engine axes: +X forward, +Y
// right, +Z up - the same convention XRWeaponPoseSolver's engine poses use),
// before conversion into any particular avatar's mesh-local space. Kept as
// plain engine math types (not XR types) so this header stays free of any XR
// provider dependency; AvatarRenderer is what converts a real
// XRSpaceSamples/XREnginePose sample into this.
struct AvatarEnginePose
{
	bool Valid = false;
	vec3 Position = vec3(0.0f);
	quaternion Orientation;
};

struct AvatarIKFrameInput
{
	AvatarEnginePose Head;
	AvatarEnginePose LeftHandGrip;
	AvatarEnginePose RightHandGrip;

	// Diagnostic-only: lets --avatar-ik-synthetic exercise the leg IK's
	// airborne blend without a real jump. Always false outside that debug
	// path (Engine.cpp only sets it behind --avatar-ik-synthetic), so it has
	// no effect on real OpenXR or desktop play.
	bool SyntheticAirborne = false;
};

// Per-foot persistent step-machine state. AvatarIKSolver's arm/pelvis solve
// is otherwise stateless/memoryless (recomputed fresh from `input` every
// call, per M2) - legs are the one part of this solver that need memory
// across frames (a planted foot must stay put between steps) and real time
// (a step takes a duration to complete), so the caller owns one of these per
// foot across frames and passes it back in every call. Default-constructed
// state means "not yet planted anywhere" - SolveWithLegs plants it on first
// use rather than assuming a location.
enum class AvatarLegStepPhase : uint8_t { Idle, SteppingForward, SteppingBack };

struct AvatarLegStepState
{
	AvatarLegStepPhase Phase = AvatarLegStepPhase::Idle;
	bool Planted = false;
	vec3 PlantedGround = vec3(0.0f);   // mesh-local; current resting/last-planted ground contact
	vec3 StepStartGround = vec3(0.0f); // mesh-local; where the in-progress step began
	vec3 StepTargetGround = vec3(0.0f);// mesh-local; where the in-progress step is heading
	float StepT = 0.0f;                // 0..1 progress through the in-progress step
	float GroundedBlend = 0.0f;        // 0 = fully airborne/neutral hang pose, 1 = fully grounded
};

struct AvatarLegIKState
{
	AvatarLegStepState Left;
	AvatarLegStepState Right;
};

// Output of estimating where each leg's hip/thigh root currently sits (after
// the same head-driven pelvis motion Solve() applies) plus this rig's own
// bind-pose up/forward axes and measured leg length - everything a caller
// needs to build a world-space ground-probe segment before calling
// SolveWithLegs. A side is left Valid == false (and its fields at their
// defaults) when that leg's rig roles are missing or degenerate - M1's
// auto-rig can still leave a mesh with an incomplete leg chain (see
// Docs/FULLBODY_VR_AVATAR_PLAN.md M3's non-goals).
struct AvatarLegRootEstimate
{
	bool PelvisDriven = false;
	bool LeftValid = false;
	bool RightValid = false;
	vec3 LeftHipMeshLocal = vec3(0.0f);
	vec3 RightHipMeshLocal = vec3(0.0f);
	float LeftLegLength = 0.0f;
	float RightLegLength = 0.0f;
	vec3 Up = vec3(0.0f, 0.0f, 1.0f);
	vec3 Forward = vec3(1.0f, 0.0f, 0.0f);
};

class AvatarIKSolver
{
public:
	// outTransforms is resized to rig.Joints.size() and filled with one
	// AvatarJointTransform per joint, ready to hand straight to
	// AvatarSkinner::Skin. A joint whose role isn't driven by any of the
	// logic below (missing input target, missing role, degenerate bone
	// length) is left following the pelvis rigidly (or at bind pose if the
	// pelvis itself isn't driven) rather than left at a stale/garbage value.
	// Never throws, never produces NaN/Inf given finite input. Arms/pelvis/
	// torso only - legs are left at the rigid pelvis-follow default; see
	// SolveWithLegs for the M3 leg-solving entry point.
	static void Solve(const AvatarRig& rig, const AvatarIKInput& input, const AvatarIKOptions& options, Array<AvatarJointTransform>& outTransforms);

	// See AvatarLegRootEstimate. Pure math, no ground probing itself - the
	// caller (AvatarRenderer) uses the returned hip positions to build the
	// actual CollisionSystem::TraceFirstHit segment.
	static AvatarLegRootEstimate EstimateLegRoots(const AvatarRig& rig, const AvatarIKInput& input, const AvatarIKOptions& options);

	// As Solve, but also solves LeftThigh/LeftCalf/LeftFoot and
	// RightThigh/RightCalf/RightFoot: a small step state machine (idle/
	// stepping-forward/stepping-back, kept per foot in `legState` across
	// calls) advances a grounded foot toward the pelvis's horizontal
	// projection once horizontal distance exceeds a threshold derived from
	// that leg's own measured length, and blends to a neutral hanging-leg
	// pose whenever `input.Grounded` is false or that foot's ground probe
	// didn't find anything. A leg missing its rig roles (or a rig with no
	// drivable pelvis) is left at Solve's rigid pelvis-follow default -
	// same graceful degradation as a missing arm role. Never throws, never
	// produces NaN/Inf given finite input.
	static void SolveWithLegs(const AvatarRig& rig, const AvatarIKInput& input, const AvatarIKOptions& options,
		float deltaTimeSeconds, AvatarLegIKState& legState, Array<AvatarJointTransform>& outTransforms);
};
