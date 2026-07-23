
#include "Precomp.h"
#include "AvatarRenderer.h"
#include "AvatarAutoRig.h"
#include "AvatarRigCache.h"
#include "AvatarSkinner.h"
#include "Engine.h"
#include "UObject/UActor.h"
#include "UObject/UMesh.h"
#include "UObject/UTexture.h"
#include "UObject/ULevel.h"
#include "Package/Package.h"
#include "Package/PackageManager.h"
#include "Render/VisibleFrame.h"
#include "Render/RenderSubsystem.h"
#include "RenderDevice/RenderDevice.h"
#include "Utils/Logger.h"

#include <cmath>
#include <sstream>
#include <set>
#include <iostream>

bool AvatarRenderer::DiagnosticsEnabledFlag = false;

namespace
{
	// Logger only flushes to disk when the Engine is destroyed, and diagnostic
	// runs are killed rather than cleanly shut down - mirror every diagnostic
	// line to stdout too so evidence is visible without a clean process exit.
	// Only used on the --avatar-autorig-debug path, never during normal play.
	void LogDiagnostic(const std::string& line)
	{
		LogMessage(line);
		std::cout << line << std::endl;
	}
}

void AvatarRenderer::SetDiagnosticsEnabled(bool enabled)
{
	DiagnosticsEnabledFlag = enabled;
}

bool AvatarRenderer::DiagnosticsEnabled()
{
	return DiagnosticsEnabledFlag;
}

namespace
{
	// Reimplementation of VisibleMesh::SetupMeshTextures - kept local (rather
	// than reusing engine->render->Mesh.textures) so the debug draw path never
	// mutates shared render state used by the normal mesh draw.
	void ResolveMeshTextures(UActor* actor, UMesh* mesh, Array<UTexture*>& outTextures, UTexture*& outEnvmap)
	{
		outTextures.resize(mesh->Textures.size());
		outEnvmap = nullptr;

		for (int i = 0; i < (int)mesh->Textures.size(); i++)
		{
			UTexture* tex = actor->GetMultiskin(i);
			if (!tex)
			{
				if (!mesh->Textures[i] || i == 0)
					tex = actor->Skin();

				if (!tex)
					tex = mesh->Textures[i];

				if (!tex)
					tex = actor->Texture();

				if (!tex)
				{
					for (int j = 0; j < (int)mesh->Textures.size(); j++)
					{
						UTexture* multiskin = actor->GetMultiskin(j);
						if (multiskin)
							tex = multiskin;
					}
				}
			}

			outTextures[i] = tex;
		}

		if (actor->Texture())
		{
			outEnvmap = actor->Texture();
		}
		else if (actor->Region().Zone && actor->Region().Zone->EnvironmentMap())
		{
			outEnvmap = actor->Region().Zone->EnvironmentMap();
		}
		else if (actor->Level()->EnvironmentMap())
		{
			outEnvmap = actor->Level()->EnvironmentMap();
		}
	}

	void LogRigDiagnostic(UMesh* mesh, const AvatarRig& rig)
	{
		std::ostringstream out;
		out << "AvatarAutoRig: mesh=" << rig.MeshIdentity
			<< " valid=" << (rig.Valid ? "1" : "0")
			<< " fallback=" << (rig.UsedFallbackTier ? "1" : "0")
			<< " frameVerts=" << rig.FrameVerts
			<< " bindFrame=" << rig.BindFrame
			<< " joints=" << rig.Joints.size();
		if (!rig.Diagnostic.empty())
			out << " note=\"" << rig.Diagnostic << "\"";
		LogDiagnostic(out.str());

		if (!rig.Valid)
		{
			if (mesh)
			{
				std::ostringstream raw;
				raw << "  raw mesh data: FrameVerts=" << mesh->FrameVerts
					<< " AnimFrames=" << mesh->AnimFrames
					<< " Tris=" << mesh->Tris.size()
					<< " Verts=" << mesh->Verts.size()
					<< " Normals=" << mesh->Normals.size();
				if (ULodMesh* lodmesh = UObject::TryCast<ULodMesh>(mesh))
					raw << " Faces=" << lodmesh->Faces.size() << " Wedges=" << lodmesh->Wedges.size();
				LogDiagnostic(raw.str());
			}
			return;
		}

		for (size_t i = 0; i < rig.Joints.size(); i++)
		{
			const AvatarJoint& joint = rig.Joints[i];
			std::string parentName = (joint.Parent >= 0 && joint.Parent < (int)rig.Joints.size()) ? AvatarJointRoleName(rig.Joints[joint.Parent].Role) : "(root)";
			std::ostringstream line;
			line << "  joint[" << i << "] role=" << AvatarJointRoleName(joint.Role)
				<< " parent=" << parentName
				<< " verts=" << joint.VertexCount
				<< " origin=(" << joint.BindOrigin.x << ", " << joint.BindOrigin.y << ", " << joint.BindOrigin.z << ")";
			LogDiagnostic(line.str());
		}

		// Bind-pose reconstruction self-check: with no joint transforms
		// supplied, AvatarSkinner must reproduce the mesh's own bind-frame
		// vertices exactly (see AvatarSkinner.cpp - identity path does no
		// floating point math at all).
		Array<vec3> positions, normals;
		AvatarSkinner::SkinBindPose(rig, positions, normals);

		int bindOffset = rig.BindFrame * rig.FrameVerts;
		float maxPosError = 0.0f;
		double sumPosError = 0.0;
		int checked = 0;
		if (mesh && mesh->Verts.size() >= (size_t)(bindOffset + rig.FrameVerts))
		{
			for (int v = 0; v < rig.FrameVerts; v++)
			{
				float err = length(positions[v] - mesh->Verts[bindOffset + v]);
				maxPosError = std::max(maxPosError, err);
				sumPosError += err;
				checked++;
			}
		}

		std::ostringstream check;
		check << "  bind-pose self-check: verticesChecked=" << checked
			<< " maxError=" << maxPosError
			<< " meanError=" << (checked > 0 ? (sumPosError / checked) : 0.0);
		LogDiagnostic(check.str());
	}
}

void AvatarRenderer::RunMapLoadDiagnostics()
{
	if (!DiagnosticsEnabledFlag)
		return;

	if (!engine || !engine->Level)
	{
		LogDiagnostic("AvatarAutoRig: RunMapLoadDiagnostics skipped - no level loaded");
		return;
	}

	std::set<UMesh*> seen;
	int meshesChecked = 0;
	const int maxMeshes = 6;

	auto tryMesh = [&](UActor* actor)
	{
		if (meshesChecked >= maxMeshes || !actor)
			return;
		if (!UObject::TryCast<UPawn>(actor))
			return;
		UMesh* mesh = actor->Mesh();
		if (!mesh || seen.count(mesh))
			return;
		seen.insert(mesh);
		meshesChecked++;

		const AvatarRig& rig = AvatarRigCache::GetOrBuild(mesh);
		LogRigDiagnostic(mesh, rig);
	};

	if (engine->viewport)
		tryMesh(engine->viewport->Actor());

	for (UActor* actor : engine->Level->Actors)
	{
		if (meshesChecked >= maxMeshes)
			break;
		tryMesh(actor);
	}

	// Widen the search past whichever single pawn is spawned in this map:
	// already-loaded packages (e.g. Botpack) hold every stock player model
	// as plain UMesh exports, so rig any with genuine multi-frame vertex
	// animation too - gives M1 evidence across more than one mesh even from
	// a map with just the local player in it.
	if (engine->packages)
	{
		for (const NameString& pkgName : engine->packages->GetPackageNames())
		{
			if (meshesChecked >= maxMeshes)
				break;

			Package* pkg = engine->packages->GetPackage(pkgName);
			if (!pkg)
				continue;

			for (UMesh* mesh : pkg->GetAllObjects<UMesh>())
			{
				if (meshesChecked >= maxMeshes)
					break;
				if (!mesh || seen.count(mesh))
					continue;
				if (mesh->FrameVerts <= 0 || mesh->AnimFrames <= 1)
					continue;

				seen.insert(mesh);
				meshesChecked++;

				const AvatarRig& rig = AvatarRigCache::GetOrBuild(mesh);
				LogRigDiagnostic(mesh, rig);
			}
		}
	}

	LogDiagnostic("AvatarAutoRig: RunMapLoadDiagnostics checked " + std::to_string(meshesChecked) + " distinct pawn mesh(es)");
}

namespace
{
	// Shared by DrawActorBindPose and DrawActorWithIK - everything from here
	// down only cares about the final skinned positions/normals, not how
	// they were produced (bind pose vs. IK-solved). See
	// ExtractTriangleVertexIndices in AvatarAutoRig.cpp for why there are two
	// mesh storage formats below.
	void DrawSkinnedMesh(VisibleFrame* frame, UActor* actor, UMesh* mesh, const Array<vec3>& skinnedPositions,
		const Array<vec3>& skinnedNormals, const vec3& worldOffset)
	{
		mat4 objectToWorld = mat4::translate(actor->Location() + actor->PrePivot() + worldOffset) * Coords::Rotation(actor->Rotation()).ToMatrix() * mat4::scale(actor->DrawScale());
		mat4 meshToWorld = objectToWorld * mesh->meshToObject;
		mat3 meshNormalToWorld = mat3::transpose(mat3(meshToWorld));

		float fatness = actor->Fatness() / 16.0f - 8.0f;

		Array<UTexture*> textures;
		UTexture* envmap = nullptr;
		ResolveMeshTextures(actor, mesh, textures, envmap);

		uint32_t polyflags = 0;
		switch (actor->Style())
		{
		case 2: polyflags |= PF_Masked; break; // STY_Masked
		case 3: polyflags |= PF_Translucent; break; // STY_Translucent
		case 4: polyflags |= PF_Modulated; break; // STY_Modulated
		}
		if (actor->bNoSmooth()) polyflags |= PF_NoSmooth;
		if (actor->bSelected()) polyflags |= PF_Selected;
		if (actor->bMeshEnviroMap()) polyflags |= PF_Environment;
		if (actor->bMeshCurvy()) polyflags |= PF_Flat;
		if (actor->bUnlit() || actor->Region().ZoneNumber == 0) polyflags |= PF_Unlit;

		UZoneInfo* zoneActor = engine->GetZoneActor(actor->Region().ZoneNumber);

		// Draws one triangle given its bind-pose vertex indices, per-corner UV
		// (already in texel space), and combined poly flags/texture. Shared by
		// both mesh storage formats below - see ExtractTriangleVertexIndices in
		// AvatarAutoRig.cpp for why there are two.
		auto drawTri = [&](const size_t vindex[3], const vec2 uv[3], uint32_t triPolyFlags, UTexture* tex)
		{
			if (!tex)
				return;

			uint32_t renderflags = triPolyFlags | polyflags;
			UTexture* drawTex = (renderflags & PF_Environment) ? envmap : tex;
			if (!drawTex)
				return;

			engine->render->UpdateTexture(drawTex);

			FTextureInfo texinfo;
			engine->render->UpdateTextureInfo(texinfo, drawTex);

			float uscale = (drawTex ? drawTex->UsedMipmaps.front().Width : 256) * (1.0f / 255.0f);
			float vscale = (drawTex ? drawTex->UsedMipmaps.front().Height : 256) * (1.0f / 255.0f);

			GouraudVertex vertices[3];
			vec3 normals[3];
			for (int i = 0; i < 3; i++)
			{
				if (vindex[i] >= skinnedPositions.size())
					return;

				vec3 vertex = skinnedPositions[vindex[i]] + skinnedNormals[vindex[i]] * fatness;
				vertices[i].Point = (meshToWorld * vec4(vertex, 1.0f)).xyz();
				vertices[i].UV = { uv[i].x * uscale, uv[i].y * vscale };
				normals[i] = normalize(meshNormalToWorld * skinnedNormals[vindex[i]]);
			}

			if (renderflags & PF_Environment)
			{
				mat3 rotmat = mat3(frame->Frame.WorldToView * frame->Frame.ObjectToWorld);
				for (int i = 0; i < 3; i++)
				{
					vec3 v = normalize(vertices[i].Point);
					vec3 p = rotmat * reflect(v, normals[i]);
					vertices[i].UV = { (p.x + 1.0f) * 128.0f * uscale, (p.y + 1.0f) * 128.0f * vscale };
				}
			}

			for (int i = 0; i < 3; i++)
			{
				vertices[i].Light = engine->render->GetVertexLight(actor, vertices[i].Point, normals[i], !!(polyflags & PF_Unlit), zoneActor);
				vertices[i].Fog = engine->render->GetVertexFog(actor, vertices[i].Point);
			}

			renderflags |= PF_RenderFog;

			frame->Device->DrawGouraudPolygon(&frame->Frame, texinfo, vertices, 3, renderflags);
		};

		if (!mesh->Tris.empty())
		{
			// Legacy flat-format mesh: triangle, UV and texture index are all
			// stored directly per-triangle.
			for (const MeshTri& tri : mesh->Tris)
			{
				if (tri.TextureIndex >= (int)textures.size())
					continue;

				size_t vindex[3] = { tri.Indices[0], tri.Indices[1], tri.Indices[2] };
				vec2 uv[3] = { vec2(tri.UV[0].x, tri.UV[0].y), vec2(tri.UV[1].x, tri.UV[1].y), vec2(tri.UV[2].x, tri.UV[2].y) };
				drawTri(vindex, uv, tri.PolyFlags, textures[tri.TextureIndex]);
			}
		}
		else if (ULodMesh* lodmesh = UObject::TryCast<ULodMesh>(mesh))
		{
			// Real stock UT99 player meshes: geometry is Faces indexing Wedges,
			// with Wedges giving both the vertex (offset by SpecialVerts, then
			// ReMapAnimVerts) and per-corner UV; texture/poly flags come from
			// the face's Material - see VisibleMesh::DrawLodMeshFace.
			for (const MeshFace& face : lodmesh->Faces)
			{
				if (face.MaterialIndex >= lodmesh->Materials.size())
					continue;

				const MeshMaterial& material = lodmesh->Materials[face.MaterialIndex];
				if (material.PolyFlags & PF_Invisible)
					continue;
				if (material.TextureIndex >= (int)textures.size())
					continue;

				size_t vindex[3];
				vec2 uv[3];
				bool ok = true;
				for (int i = 0; i < 3; i++)
				{
					size_t wedgeIndex = face.Indices[i];
					if (wedgeIndex >= lodmesh->Wedges.size())
					{
						ok = false;
						break;
					}
					const MeshWedge& wedge = lodmesh->Wedges[wedgeIndex];
					size_t vbase = (size_t)wedge.Vertex + lodmesh->SpecialVerts;
					if (!lodmesh->ReMapAnimVerts.empty() && vbase >= lodmesh->ReMapAnimVerts.size())
					{
						ok = false;
						break;
					}
					vindex[i] = lodmesh->ReMapAnimVerts.empty() ? vbase : lodmesh->ReMapAnimVerts[vbase];
					uv[i] = vec2(wedge.U, wedge.V);
				}
				if (!ok)
					continue;

				drawTri(vindex, uv, material.PolyFlags, textures[material.TextureIndex]);
			}
		}
	}

	AvatarIKTarget ConvertEnginePoseToMeshLocal(const mat3& worldToMeshLinear, const vec3& meshOriginWorld, const AvatarEnginePose& pose)
	{
		AvatarIKTarget target;
		if (!pose.Valid)
			return target;
		target.Valid = true;
		target.Position = worldToMeshLinear * (pose.Position - meshOriginWorld);
		vec3 forwardWorld = normalize(pose.Orientation * vec3(1.0f, 0.0f, 0.0f));
		vec3 upWorld = normalize(pose.Orientation * vec3(0.0f, 0.0f, 1.0f));
		target.Forward = normalize(worldToMeshLinear * forwardWorld);
		target.Up = normalize(worldToMeshLinear * upWorld);
		return target;
	}
}

bool AvatarRenderer::DrawActorBindPose(VisibleFrame* frame, UActor* actor, const vec3& worldOffset)
{
	if (!frame || !actor)
		return false;

	UMesh* mesh = actor->Mesh();
	if (!mesh)
		return false;

	const AvatarRig& rig = AvatarRigCache::GetOrBuild(mesh);
	if (!rig.Valid)
		return false;

	Array<vec3> skinnedPositions, skinnedNormals;
	AvatarSkinner::SkinBindPose(rig, skinnedPositions, skinnedNormals);
	if ((int)skinnedPositions.size() != rig.FrameVerts)
		return false;

	DrawSkinnedMesh(frame, actor, mesh, skinnedPositions, skinnedNormals, worldOffset);
	return true;
}

AvatarIKInput AvatarRenderer::BuildIKInput(UActor* actor, UMesh* mesh, const AvatarIKFrameInput& engineInput)
{
	// Deliberately no worldOffset here - that only exists to place the debug
	// draw beside the actor's real render, and must not leak into where the
	// real head/hand poses land in mesh-local space.
	mat4 objectToWorld = mat4::translate(actor->Location() + actor->PrePivot()) * Coords::Rotation(actor->Rotation()).ToMatrix() * mat4::scale(actor->DrawScale());
	mat4 meshToWorld = objectToWorld * mesh->meshToObject;
	mat3 meshToWorldLinear(meshToWorld);
	mat3 worldToMeshLinear = mat3::inverse(meshToWorldLinear);
	vec3 meshOriginWorld = (meshToWorld * vec4(0.0f, 0.0f, 0.0f, 1.0f)).xyz();

	AvatarIKInput input;
	input.Head = ConvertEnginePoseToMeshLocal(worldToMeshLinear, meshOriginWorld, engineInput.Head);
	input.LeftHand = ConvertEnginePoseToMeshLocal(worldToMeshLinear, meshOriginWorld, engineInput.LeftHandGrip);
	input.RightHand = ConvertEnginePoseToMeshLocal(worldToMeshLinear, meshOriginWorld, engineInput.RightHandGrip);
	return input;
}

bool AvatarRenderer::DrawActorWithIK(VisibleFrame* frame, UActor* actor, const vec3& worldOffset,
	const AvatarIKFrameInput& engineInput, const AvatarIKOptions& options)
{
	if (!frame || !actor)
		return false;

	UMesh* mesh = actor->Mesh();
	if (!mesh)
		return false;

	const AvatarRig& rig = AvatarRigCache::GetOrBuild(mesh);
	if (!rig.Valid)
		return false;

	AvatarIKInput ikInput = BuildIKInput(actor, mesh, engineInput);
	Array<AvatarJointTransform> jointTransforms;
	AvatarIKSolver::Solve(rig, ikInput, options, jointTransforms);

	Array<vec3> skinnedPositions, skinnedNormals;
	AvatarSkinner::Skin(rig, &jointTransforms, skinnedPositions, skinnedNormals);
	if ((int)skinnedPositions.size() != rig.FrameVerts)
		return false;

	DrawSkinnedMesh(frame, actor, mesh, skinnedPositions, skinnedNormals, worldOffset);
	return true;
}

AvatarIKFrameInput AvatarRenderer::BuildSyntheticFrameInput(const vec3& cameraLocation, float timeSeconds)
{
	// Deterministic, headset-free stand-in for a real XR sample (see
	// Docs/FULLBODY_VR_AVATAR_PLAN.md milestone M2's verification note) -
	// only used behind --avatar-ik-synthetic, when no OpenXR session is
	// driving the avatar. Moves the head in a small circle and both hands
	// through a slow forward/back reach so DrawActorWithIK's effect is
	// visible without a headset.
	AvatarIKFrameInput input;

	const float headBob = 6.0f;
	vec3 headOffset(std::cos(timeSeconds * 0.5f) * headBob, std::sin(timeSeconds * 0.5f) * headBob, 0.0f);
	input.Head.Valid = true;
	input.Head.Position = cameraLocation + headOffset;

	const float reach = 40.0f + std::sin(timeSeconds * 0.75f) * 20.0f;
	input.LeftHandGrip.Valid = true;
	input.LeftHandGrip.Position = cameraLocation + vec3(reach, -25.0f, -10.0f);

	input.RightHandGrip.Valid = true;
	input.RightHandGrip.Position = cameraLocation + vec3(reach, 25.0f, -10.0f);

	return input;
}
