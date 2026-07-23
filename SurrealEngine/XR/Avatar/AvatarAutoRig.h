#pragma once

// Runtime auto-rig: rebuilds a small biped joint hierarchy and per-vertex
// skin weights directly from a UMesh's own vertex-anim frames. See
// Docs/FULLBODY_VR_AVATAR_PLAN.md ("Runtime auto-rig") for the algorithm
// this implements.

#include "Math/vec.h"
#include "Utils/Array.h"
#include <cstdint>
#include <string>

class UMesh;

enum class AvatarJointRole : uint8_t
{
	Unknown = 0,
	Pelvis,
	Spine,
	Chest,
	Neck,
	Head,
	LeftShoulder,
	LeftUpperArm,
	LeftForearm,
	LeftHand,
	RightShoulder,
	RightUpperArm,
	RightForearm,
	RightHand,
	LeftThigh,
	LeftCalf,
	LeftFoot,
	RightThigh,
	RightCalf,
	RightFoot,
	Count
};

const char* AvatarJointRoleName(AvatarJointRole role);

// True for the small set of roles that are allowed to appear more than once
// (there is currently none - every canonical role is unique), kept as a
// named helper so the rig builder and any future role additions agree on it.
int AvatarJointRoleParent(AvatarJointRole role);

struct AvatarJoint
{
	AvatarJointRole Role = AvatarJointRole::Unknown;
	int Parent = -1;             // index into AvatarRig::Joints, -1 for the root (Pelvis)
	vec3 BindOrigin = vec3(0.0f); // cluster centroid in the bind frame, raw mesh-local space
	int VertexCount = 0;         // vertices weighted to this joint, informational/logging only
};

struct AvatarRig
{
	bool Valid = false;
	bool UsedFallbackTier = false;
	std::string Diagnostic; // human-readable note: fallback reason, or "not riggable" cause

	int FrameVerts = 0;
	int BindFrame = 0; // which animation frame index was treated as the bind pose

	Array<AvatarJoint> Joints;
	Array<uint8_t> VertexJoint; // size FrameVerts; index into Joints, one entry per bind-pose vertex
	Array<vec3> BindVertices;   // size FrameVerts; copy of mesh->Verts at BindFrame
	Array<vec3> BindNormals;    // size FrameVerts; copy of mesh->Normals at BindFrame

	uint64_t MeshContentHash = 0;
	std::string MeshIdentity; // "Package.ObjectName", used for logging and as a cache key fallback
};

class AvatarAutoRig
{
public:
	// Builds a rig from the mesh's own Verts/Tris/AnimSeqs. Never throws; on
	// any degenerate input returns a rig with Valid == false and Diagnostic
	// explaining why (see AvatarRig::Diagnostic).
	static AvatarRig Build(UMesh* mesh);

	// Content hash over Verts/Tris, independent of package/object naming, so
	// the cache can validate a name-based match still refers to the same data.
	static uint64_t ComputeMeshContentHash(UMesh* mesh);
};
