
#include "Precomp.h"
#include "AvatarSkinner.h"

void AvatarSkinner::Skin(const AvatarRig& rig, const Array<AvatarJointTransform>* jointTransforms, Array<vec3>& outPositions, Array<vec3>& outNormals)
{
	int n = rig.FrameVerts;
	outPositions.resize(n);
	outNormals.resize(n);

	for (int v = 0; v < n; v++)
	{
		int j = (v < (int)rig.VertexJoint.size()) ? rig.VertexJoint[v] : 0;
		vec3 bindOrigin = (j >= 0 && j < (int)rig.Joints.size()) ? rig.Joints[j].BindOrigin : vec3(0.0f);

		if (jointTransforms && j >= 0 && j < (int)jointTransforms->size())
		{
			const AvatarJointTransform& t = (*jointTransforms)[j];
			vec3 localPos = rig.BindVertices[v] - bindOrigin;
			outPositions[v] = t.Rotation * localPos + bindOrigin + t.Translation;
			outNormals[v] = normalize(t.Rotation * rig.BindNormals[v]);
		}
		else
		{
			// No transform supplied for this joint - skin at bind pose exactly
			// (this is the only path M1 exercises).
			outPositions[v] = rig.BindVertices[v];
			outNormals[v] = rig.BindNormals[v];
		}
	}
}

void AvatarSkinner::SkinBindPose(const AvatarRig& rig, Array<vec3>& outPositions, Array<vec3>& outNormals)
{
	Skin(rig, nullptr, outPositions, outNormals);
}
