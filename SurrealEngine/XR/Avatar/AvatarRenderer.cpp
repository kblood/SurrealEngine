
#include "Precomp.h"
#include "AvatarRenderer.h"
#include "AvatarAutoRig.h"
#include "AvatarRigCache.h"
#include "AvatarSkinner.h"
#include "Engine.h"
#include "XR/XRWeaponPoseSolver.h"
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
#include "Collision/TopLevel/CollisionSystem.h"
#include "Collision/TopLevel/CollisionHit.h"

#include <cmath>
#include <sstream>
#include <set>
#include <iostream>

bool AvatarRenderer::EnabledFlag = false;
bool AvatarRenderer::DiagnosticsEnabledFlag = false;
bool AvatarRenderer::CullHeadDebugEnabledFlag = false;

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

void AvatarRenderer::SetEnabled(bool enabled)
{
	EnabledFlag = enabled;
}

bool AvatarRenderer::Enabled()
{
	return EnabledFlag;
}

void AvatarRenderer::SetDiagnosticsEnabled(bool enabled)
{
	DiagnosticsEnabledFlag = enabled;
}

bool AvatarRenderer::DiagnosticsEnabled()
{
	return DiagnosticsEnabledFlag;
}

void AvatarRenderer::SetCullHeadDebugEnabled(bool enabled)
{
	CullHeadDebugEnabledFlag = enabled;
}

bool AvatarRenderer::CullHeadDebugEnabled()
{
	return CullHeadDebugEnabledFlag;
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

	int FindRigJointIndex(const AvatarRig& rig, AvatarJointRole role)
	{
		for (size_t i = 0; i < rig.Joints.size(); i++)
			if (rig.Joints[i].Role == role)
				return (int)i;
		return -1;
	}

	// M5: for any real mesh whose auto-rig has a complete right arm chain,
	// proves against that mesh's own bind-pose geometry that the arm IK's
	// hand target and the weapon's grip-anchored visual pose (zero local
	// offset - see XRWeaponPoseSolver.cpp) land at the same point. Same
	// claim as Tests/AvatarIKSolverTests.cpp's TestHandTracksSimulatedGripSequence,
	// just run per real mesh instead of a synthetic rig - a sibling check to
	// the bind-pose self-check above.
	void LogHandWeaponReconciliationCheck(const AvatarRig& rig)
	{
		int upperArmIdx = FindRigJointIndex(rig, AvatarJointRole::RightUpperArm);
		int forearmIdx = FindRigJointIndex(rig, AvatarJointRole::RightForearm);
		int handIdx = FindRigJointIndex(rig, AvatarJointRole::RightHand);
		if (upperArmIdx < 0 || forearmIdx < 0 || handIdx < 0)
			return;

		const vec3& upperArmBind = rig.Joints[upperArmIdx].BindOrigin;
		const vec3& handBind = rig.Joints[handIdx].BindOrigin;

		// 70% of the way from shoulder to the bind-pose hand - within this
		// arm's own measured reach regardless of this mesh's scale/axes.
		vec3 target = mix(upperArmBind, handBind, 0.7f);

		AvatarIKInput input;
		input.RightHand.Valid = true;
		input.RightHand.Position = target;
		AvatarIKOptions options;
		Array<AvatarJointTransform> transforms;
		AvatarIKSolver::Solve(rig, input, options, transforms);

		vec3 solvedHand = rig.Joints[handIdx].BindOrigin + transforms[handIdx].Translation;
		float gripTargetGap = length(solvedHand - target);

		XREnginePose grip;
		grip.Valid = true;
		grip.Position = { target.x, target.y, target.z };
		XRWeaponPoseOptions weaponOptions;
		weaponOptions.VisualAnchor = XRWeaponVisualAnchor::Grip;
		XRWeaponPoseResult weaponPose = SolveXRWeaponPose(grip, grip, XRHand::Right, weaponOptions);
		vec3 weaponWorld(weaponPose.VisualPose.Position.X, weaponPose.VisualPose.Position.Y, weaponPose.VisualPose.Position.Z);
		float weaponVisualGap = length(solvedHand - weaponWorld);

		std::ostringstream out;
		out << "  M5 hand/weapon-grip check: target=(" << target.x << "," << target.y << "," << target.z << ")"
			<< " solvedHand=(" << solvedHand.x << "," << solvedHand.y << "," << solvedHand.z << ")"
			<< " gripTargetGap=" << gripTargetGap
			<< " weaponVisualGap=" << weaponVisualGap;
		LogDiagnostic(out.str());
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

		LogHandWeaponReconciliationCheck(rig);
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
	// Mesh<->world conversion shared by BuildIKInput, DrawActorWithIK's ground
	// probing, and the final skinned draw - factored out so the M4
	// calibration scale is threaded into every one of those consistently
	// instead of three independent copies of the same formula silently
	// drifting apart. `extraScale` folds in on top of the actor's own
	// DrawScale (1.0 = no calibration, i.e. identical to every prior
	// milestone's behavior).
	struct MeshWorldTransform
	{
		mat4 MeshToWorld;
		mat3 MeshToWorldLinear;
		mat3 WorldToMeshLinear;
		vec3 MeshOriginWorld;
	};

	MeshWorldTransform ComputeMeshWorldTransform(UActor* actor, UMesh* mesh, const vec3& worldOffset, float extraScale)
	{
		MeshWorldTransform result;
		mat4 objectToWorld = mat4::translate(actor->Location() + actor->PrePivot() + worldOffset) *
			Coords::Rotation(actor->Rotation()).ToMatrix() * mat4::scale(actor->DrawScale() * extraScale);
		result.MeshToWorld = objectToWorld * mesh->meshToObject;
		result.MeshToWorldLinear = mat3(result.MeshToWorld);
		result.WorldToMeshLinear = mat3::inverse(result.MeshToWorldLinear);
		result.MeshOriginWorld = (result.MeshToWorld * vec4(0.0f, 0.0f, 0.0f, 1.0f)).xyz();
		return result;
	}

	// Shared by DrawActorBindPose and DrawActorWithIK - everything from here
	// down only cares about the final skinned positions/normals, not how
	// they were produced (bind pose vs. IK-solved). See
	// ExtractTriangleVertexIndices in AvatarAutoRig.cpp for why there are two
	// mesh storage formats below.
	void DrawSkinnedMesh(VisibleFrame* frame, UActor* actor, UMesh* mesh, const AvatarRig& rig, const Array<vec3>& skinnedPositions,
		const Array<vec3>& skinnedNormals, const vec3& worldOffset, float calibrationScale, bool cullHeadForFirstPerson)
	{
		MeshWorldTransform xform = ComputeMeshWorldTransform(actor, mesh, worldOffset, calibrationScale);
		mat4 meshToWorld = xform.MeshToWorld;
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

		// M4 diagnostics: trianglesConsidered/trianglesHeadNeck are counted
		// regardless of cullHeadForFirstPerson (see the periodic log line
		// below) so a single run's log line can prove the cull-triangle-count
		// math without needing two separate runs.
		uint32_t trianglesConsidered = 0;
		uint32_t trianglesHeadNeck = 0;
		uint32_t trianglesEmitted = 0;

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

			trianglesConsidered++;
			bool referencesHeadOrNeck = AvatarSkinner::IsHeadOrNeckVertex(rig, (int)vindex[0]) ||
				AvatarSkinner::IsHeadOrNeckVertex(rig, (int)vindex[1]) || AvatarSkinner::IsHeadOrNeckVertex(rig, (int)vindex[2]);
			if (referencesHeadOrNeck)
				trianglesHeadNeck++;
			if (cullHeadForFirstPerson && referencesHeadOrNeck)
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
			trianglesEmitted++;
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

		// Rate-limited so a live run logs a handful of readable samples
		// instead of one line per drawn frame (both draw paths call this, so
		// this is deliberately its own counter rather than sharing
		// LogSolvedJointsPeriodically's). trianglesConsidered/trianglesHeadNeck
		// are always counted (see above), so this one line proves the M4
		// verification claim ("emitted count drops by exactly the head/neck
		// triangle count") whether or not culling is actually enabled this call.
		static int drawCallCounter = 0;
		drawCallCounter++;
		if (drawCallCounter % 30 == 1)
		{
			std::ostringstream out;
			out << "AvatarRenderer: mesh=" << rig.MeshIdentity
				<< " cullHeadForFirstPerson=" << (cullHeadForFirstPerson ? "1" : "0")
				<< " trianglesConsidered=" << trianglesConsidered
				<< " trianglesHeadNeck=" << trianglesHeadNeck
				<< " trianglesEmitted=" << trianglesEmitted;
			LogDiagnostic(out.str());
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

	// Bind-pose comparison draw: never calibrated (calibrationScale=1), never
	// culls the head (this is the "see the whole rig" debug view).
	DrawSkinnedMesh(frame, actor, mesh, rig, skinnedPositions, skinnedNormals, worldOffset, 1.0f, false);
	return true;
}

AvatarIKInput AvatarRenderer::BuildIKInput(UActor* actor, UMesh* mesh, const AvatarIKFrameInput& engineInput, float calibrationScale)
{
	// Deliberately no worldOffset here - that only exists to place the debug
	// draw beside the actor's real render, and must not leak into where the
	// real head/hand poses land in mesh-local space.
	MeshWorldTransform xform = ComputeMeshWorldTransform(actor, mesh, vec3(0.0f), calibrationScale);

	AvatarIKInput input;
	input.Head = ConvertEnginePoseToMeshLocal(xform.WorldToMeshLinear, xform.MeshOriginWorld, engineInput.Head);
	input.LeftHand = ConvertEnginePoseToMeshLocal(xform.WorldToMeshLinear, xform.MeshOriginWorld, engineInput.LeftHandGrip);
	input.RightHand = ConvertEnginePoseToMeshLocal(xform.WorldToMeshLinear, xform.MeshOriginWorld, engineInput.RightHandGrip);
	return input;
}

namespace
{
	int FindJointIndex(const AvatarRig& rig, AvatarJointRole role)
	{
		for (int i = 0; i < (int)rig.Joints.size(); i++)
		{
			if (rig.Joints[i].Role == role)
				return i;
		}
		return -1;
	}

	const char* LegStepPhaseName(AvatarLegStepPhase phase)
	{
		switch (phase)
		{
		case AvatarLegStepPhase::Idle: return "idle";
		case AvatarLegStepPhase::SteppingForward: return "steppingForward";
		case AvatarLegStepPhase::SteppingBack: return "steppingBack";
		default: return "unknown";
		}
	}

	// Rate-limited so a live run logs a handful of readable samples instead of
	// one line per frame. Mesh-local space, matching AvatarIKSolver's inputs -
	// see AvatarIKSolverTests for the same joints checked against a synthetic
	// rig; this is that solver run against real player mesh data instead.
	void LogSolvedJointsPeriodically(const AvatarRig& rig, const AvatarIKInput& ikInput, const Array<AvatarJointTransform>& jointTransforms,
		const AvatarLegIKState& legState)
	{
		static int frameCounter = 0;
		frameCounter++;
		if (frameCounter % 30 != 1)
			return;

		int headIdx = FindJointIndex(rig, AvatarJointRole::Head);
		int leftHandIdx = FindJointIndex(rig, AvatarJointRole::LeftHand);
		int rightHandIdx = FindJointIndex(rig, AvatarJointRole::RightHand);
		int leftForearmIdx = FindJointIndex(rig, AvatarJointRole::LeftForearm);
		int leftFootIdx = FindJointIndex(rig, AvatarJointRole::LeftFoot);
		int rightFootIdx = FindJointIndex(rig, AvatarJointRole::RightFoot);

		std::ostringstream out;
		out << "AvatarIK: frame " << frameCounter << " mesh=" << rig.MeshIdentity;
		if (headIdx >= 0 && ikInput.Head.Valid)
		{
			vec3 headPos = rig.Joints[headIdx].BindOrigin + jointTransforms[headIdx].Translation;
			out << " head=(" << headPos.x << "," << headPos.y << "," << headPos.z << ")";
		}
		if (leftHandIdx >= 0 && ikInput.LeftHand.Valid)
		{
			vec3 handPos = rig.Joints[leftHandIdx].BindOrigin + jointTransforms[leftHandIdx].Translation;
			vec3 err = handPos - ikInput.LeftHand.Position;
			out << " leftHand=(" << handPos.x << "," << handPos.y << "," << handPos.z << ") err=" << length(err);
		}
		if (rightHandIdx >= 0 && ikInput.RightHand.Valid)
		{
			vec3 handPos = rig.Joints[rightHandIdx].BindOrigin + jointTransforms[rightHandIdx].Translation;
			vec3 err = handPos - ikInput.RightHand.Position;
			out << " rightHand=(" << handPos.x << "," << handPos.y << "," << handPos.z << ") err=" << length(err);
		}
		if (leftForearmIdx >= 0)
		{
			vec3 elbowPos = rig.Joints[leftForearmIdx].BindOrigin + jointTransforms[leftForearmIdx].Translation;
			out << " leftElbow=(" << elbowPos.x << "," << elbowPos.y << "," << elbowPos.z << ")";
		}
		out << " grounded=" << (ikInput.Grounded ? "1" : "0");
		if (leftFootIdx >= 0)
		{
			vec3 footPos = rig.Joints[leftFootIdx].BindOrigin + jointTransforms[leftFootIdx].Translation;
			out << " leftFoot=(" << footPos.x << "," << footPos.y << "," << footPos.z << ")"
				<< " leftFootGroundValid=" << (ikInput.LeftFootGround.Valid ? "1" : "0")
				<< " leftFootGroundY=" << ikInput.LeftFootGround.GroundPoint.z
				<< " leftPhase=" << LegStepPhaseName(legState.Left.Phase)
				<< " leftBlend=" << legState.Left.GroundedBlend;
		}
		if (rightFootIdx >= 0)
		{
			vec3 footPos = rig.Joints[rightFootIdx].BindOrigin + jointTransforms[rightFootIdx].Translation;
			out << " rightFoot=(" << footPos.x << "," << footPos.y << "," << footPos.z << ")"
				<< " rightFootGroundValid=" << (ikInput.RightFootGround.Valid ? "1" : "0")
				<< " rightFootGroundY=" << ikInput.RightFootGround.GroundPoint.z
				<< " rightPhase=" << LegStepPhaseName(legState.Right.Phase)
				<< " rightBlend=" << legState.Right.GroundedBlend;
		}
		LogDiagnostic(out.str());
	}

	// M5: proves the avatar's solved right-hand joint lands on the same world
	// point the weapon's grip-anchored visual pose uses. `xform` must be the
	// worldOffset=0 transform (same one BuildIKInput used) so this measures
	// the real targeting math, not the side-by-side debug draw's cosmetic
	// shift. `rightHandGripWorld` is the same engine-space grip pose Engine.cpp
	// feeds both this solver (via BuildIKInput) and, when avatar diagnostics
	// are enabled, SolveXRWeaponPose's Grip anchor - so gripTargetGap is the
	// avatar-side half of the reconciliation. weaponVisualGap prefers the
	// real rendered weapon pose (real XR session); when none is running (no
	// headset - this workspace's usual case, see BuildSyntheticFrameInput)
	// it falls back to calling the real SolveXRWeaponPose with the same grip
	// sample directly, purely to produce comparable evidence - this fallback
	// never touches engine gameplay/render state, only local logging output.
	void LogHandWeaponAlignment(const AvatarRig& rig, const MeshWorldTransform& xform,
		const Array<AvatarJointTransform>& jointTransforms, const AvatarEnginePose& rightHandGripWorld)
	{
		static int frameCounter = 0;
		frameCounter++;
		if (frameCounter % 30 != 1)
			return;

		int rightHandIdx = FindJointIndex(rig, AvatarJointRole::RightHand);
		if (rightHandIdx < 0 || !rightHandGripWorld.Valid)
			return;

		vec3 handMeshLocal = rig.Joints[rightHandIdx].BindOrigin + jointTransforms[rightHandIdx].Translation;
		vec3 handWorld = (xform.MeshToWorld * vec4(handMeshLocal, 1.0f)).xyz();
		float gripTargetGap = length(handWorld - rightHandGripWorld.Position);

		XRWeaponPoseResult weaponPose = engine->GetXRWeaponPose();
		const bool liveWeaponPose = weaponPose.Valid;
		if (!liveWeaponPose)
		{
			XREnginePose grip;
			grip.Valid = true;
			grip.Position = { rightHandGripWorld.Position.x, rightHandGripWorld.Position.y, rightHandGripWorld.Position.z };
			grip.Orientation = { rightHandGripWorld.Orientation.x, rightHandGripWorld.Orientation.y,
				rightHandGripWorld.Orientation.z, rightHandGripWorld.Orientation.w };
			XRWeaponPoseOptions options;
			options.VisualAnchor = XRWeaponVisualAnchor::Grip;
			weaponPose = SolveXRWeaponPose(grip, grip, XRHand::Right, options);
		}

		std::ostringstream out;
		out << "AvatarWeaponAlign: frame " << frameCounter
			<< " handWorld=(" << handWorld.x << "," << handWorld.y << "," << handWorld.z << ")"
			<< " gripTargetGap=" << gripTargetGap;
		if (weaponPose.Valid)
		{
			vec3 weaponWorld(weaponPose.VisualPose.Position.X, weaponPose.VisualPose.Position.Y, weaponPose.VisualPose.Position.Z);
			float weaponVisualGap = length(handWorld - weaponWorld);
			out << " weaponWorld=(" << weaponWorld.x << "," << weaponWorld.y << "," << weaponWorld.z << ")"
				<< " weaponVisualGap=" << weaponVisualGap
				<< " weaponAnchor=" << (weaponPose.VisualAnchor == XRWeaponVisualAnchor::Grip ? "grip" : "aim")
				<< " weaponSource=" << (liveWeaponPose ? "live" : "derived");
		}
		else
		{
			out << " weaponWorld=none";
		}
		LogDiagnostic(out.str());
	}
}

namespace
{
	// M3: straight-down ground probe for one foot, from its live hip position
	// (already solved for pelvis motion) down through this leg's own measured
	// length plus a safety margin - see AvatarIKSolver::EstimateLegRoots and
	// UActor::Trace (UObject/UActor.cpp) for the same TraceFirstHit calling
	// convention this mirrors. A small fixed extents is used (this is a
	// foot-sized point probe, not a full pawn-sized sweep) - unlike the step
	// threshold, probe reach is sized off the leg's own measured length, not
	// hardcoded, so it scales with the mesh being probed.
	void ProbeFootGround(UActor* actor, const mat4& meshToWorld, const mat3& meshToWorldLinear, const mat3& worldToMeshLinear,
		const vec3& meshOriginWorld, const vec3& upMeshLocal, bool valid, const vec3& hipMeshLocal, float legLength,
		AvatarLegGroundProbe& outProbe)
	{
		if (!valid || !actor->XLevel())
			return;

		vec3 upWorld = normalize(meshToWorldLinear * upMeshLocal);
		vec3 hipWorld = (meshToWorld * vec4(hipMeshLocal, 1.0f)).xyz();
		float probeDistance = std::max(legLength, 1.0f) * 2.0f;
		vec3 probeFrom = hipWorld;
		vec3 probeTo = hipWorld - upWorld * probeDistance;

		TraceFlags flags;
		flags.world = true;
		vec3 extents(1.0f, 1.0f, 1.0f);
		CollisionHit hit = actor->XLevel()->Collision.TraceFirstHit(probeFrom, probeTo, actor, extents, flags);
		if (!hit.Actor)
			return; // no ground within probe range - leave outProbe.Valid false

		vec3 hitWorld = probeFrom + (probeTo - probeFrom) * hit.Fraction;
		outProbe.Valid = true;
		outProbe.GroundPoint = worldToMeshLinear * (hitWorld - meshOriginWorld);
	}

	// M4: straight-down probe from the tracked head position to find the real
	// floor height in world units - same TraceFirstHit convention as
	// ProbeFootGround above, just anchored at the head instead of a hip.
	// Returns false (outHeightAboveFloor left at 0) if no ground was found
	// within range; ComputeAvatarCalibration then falls back to scale 1.0
	// unless a manual override is set.
	bool ProbeHeadFloorHeight(UActor* actor, const vec3& headWorld, const vec3& upWorld, float probeDistance, float& outHeightAboveFloor)
	{
		if (!actor->XLevel())
			return false;

		vec3 probeFrom = headWorld;
		vec3 probeTo = headWorld - upWorld * probeDistance;

		TraceFlags flags;
		flags.world = true;
		vec3 extents(1.0f, 1.0f, 1.0f);
		CollisionHit hit = actor->XLevel()->Collision.TraceFirstHit(probeFrom, probeTo, actor, extents, flags);
		if (!hit.Actor)
			return false;

		vec3 hitWorld = probeFrom + (probeTo - probeFrom) * hit.Fraction;
		outHeightAboveFloor = dot(headWorld - hitWorld, upWorld);
		return outHeightAboveFloor > 0.0f;
	}

	// M4: derives the calibration scale for `rig` as worn by `actor`, from a
	// real tracked head height above a floor probe versus the rig's own
	// bind-pose head height (AvatarIKSolver::EstimateRigHeadHeight /
	// ComputeCalibration), or from options.ManualScaleOverride when set. The
	// rig's own reference height is measured at calibrationScale=1 (an
	// uncalibrated reference), never against an already-scaled transform -
	// that would be circular.
	AvatarCalibrationResult ComputeAvatarCalibration(UActor* actor, UMesh* mesh, const AvatarRig& rig,
		const AvatarEnginePose& headPose, const AvatarIKOptions& options)
	{
		AvatarRigHeightEstimate heightEstimate = AvatarIKSolver::EstimateRigHeadHeight(rig);

		float rigReferenceWorld = 0.0f;
		float trackedWorld = 0.0f;
		vec3 upWorld(0.0f, 0.0f, 1.0f);

		if (heightEstimate.Valid)
		{
			MeshWorldTransform base = ComputeMeshWorldTransform(actor, mesh, vec3(0.0f), 1.0f);
			upWorld = normalize(base.MeshToWorldLinear * heightEstimate.Up);
			rigReferenceWorld = length(base.MeshToWorldLinear * (heightEstimate.Up * heightEstimate.Height));
		}

		if (headPose.Valid && actor->XLevel())
		{
			// Probe reach derived from whichever measured quantity is
			// available (rig height, or the actor's own collision height as
			// a fallback) rather than a bare constant.
			float probeDistance = std::max(std::max(rigReferenceWorld, actor->CollisionHeight()), 1.0f) * 3.0f;
			float heightAboveFloor = 0.0f;
			if (ProbeHeadFloorHeight(actor, headPose.Position, upWorld, probeDistance, heightAboveFloor))
				trackedWorld = heightAboveFloor;
		}

		AvatarCalibrationInput calInput;
		calInput.RigReferenceHeadHeight = rigReferenceWorld;
		calInput.TrackedHeadHeightAboveFloor = trackedWorld;
		calInput.HasManualScaleOverride = options.HasManualScaleOverride;
		calInput.ManualScaleOverride = options.ManualScaleOverride;
		AvatarCalibrationResult result = AvatarIKSolver::ComputeCalibration(calInput);

		// Rate-limited, same pattern as the other AvatarRenderer diagnostics -
		// this is the evidence trail for the M4 calibration verification.
		static int calibCounter = 0;
		calibCounter++;
		if (calibCounter % 30 == 1)
		{
			std::ostringstream out;
			out << "AvatarCalibration: mesh=" << rig.MeshIdentity
				<< " rigReferenceHeadHeight=" << rigReferenceWorld
				<< " trackedHeadHeightAboveFloor=" << trackedWorld
				<< " manualOverride=" << (options.HasManualScaleOverride ? "1" : "0")
				<< " scale=" << result.Scale
				<< " autoDetected=" << (result.AutoDetected ? "1" : "0");
			LogDiagnostic(out.str());
		}

		return result;
	}
}

bool AvatarRenderer::DrawActorWithIK(VisibleFrame* frame, UActor* actor, const vec3& worldOffset,
	const AvatarIKFrameInput& engineInput, const AvatarIKOptions& options, float deltaTimeSeconds)
{
	if (!frame || !actor)
		return false;

	UMesh* mesh = actor->Mesh();
	if (!mesh)
		return false;

	const AvatarRig& rig = AvatarRigCache::GetOrBuild(mesh);
	if (!rig.Valid)
		return false;

	AvatarCalibrationResult calibration = ComputeAvatarCalibration(actor, mesh, rig, engineInput.Head, options);

	AvatarIKInput ikInput = BuildIKInput(actor, mesh, engineInput, calibration.Scale);

	// M3: overall locomotion state - legs blend to a neutral hanging pose
	// instead of ground-probing while airborne/falling/swimming/flying (see
	// EPhysics in UObject/UActor.h; this branch has not yet ported vr-m2's
	// controller-aim swim/crouch work, so Physics() is the real, already-
	// existing signal to read here).
	uint8_t physics = actor->Physics();
	ikInput.Grounded = physics != PHYS_Falling && physics != PHYS_Swimming && physics != PHYS_Flying
		&& !engineInput.SyntheticAirborne;

	MeshWorldTransform xform = ComputeMeshWorldTransform(actor, mesh, vec3(0.0f), calibration.Scale);

	AvatarLegRootEstimate legRoots = AvatarIKSolver::EstimateLegRoots(rig, ikInput, options);
	if (legRoots.PelvisDriven)
	{
		ProbeFootGround(actor, xform.MeshToWorld, xform.MeshToWorldLinear, xform.WorldToMeshLinear, xform.MeshOriginWorld,
			legRoots.Up, legRoots.LeftValid, legRoots.LeftHipMeshLocal, legRoots.LeftLegLength, ikInput.LeftFootGround);
		ProbeFootGround(actor, xform.MeshToWorld, xform.MeshToWorldLinear, xform.WorldToMeshLinear, xform.MeshOriginWorld,
			legRoots.Up, legRoots.RightValid, legRoots.RightHipMeshLocal, legRoots.RightLegLength, ikInput.RightFootGround);
	}

	// Persists across frames (planted foot position, step phase, grounded
	// blend) for whichever single avatar is being drawn - only the local
	// player's own avatar is ever drawn (see plan doc non-goals), so one
	// static instance is sufficient; a large horizontal jump (map travel,
	// respawn) is treated as a teleport snap rather than an animated step -
	// see AvatarIKSolver.cpp's teleportThreshold.
	static AvatarLegIKState legState;

	Array<AvatarJointTransform> jointTransforms;
	AvatarIKSolver::SolveWithLegs(rig, ikInput, options, deltaTimeSeconds, legState, jointTransforms);

	Array<vec3> skinnedPositions, skinnedNormals;
	AvatarSkinner::Skin(rig, &jointTransforms, skinnedPositions, skinnedNormals);
	if ((int)skinnedPositions.size() != rig.FrameVerts)
		return false;

	LogSolvedJointsPeriodically(rig, ikInput, jointTransforms, legState);
	LogHandWeaponAlignment(rig, xform, jointTransforms, engineInput.RightHandGrip);

	DrawSkinnedMesh(frame, actor, mesh, rig, skinnedPositions, skinnedNormals, worldOffset, calibration.Scale, options.CullHeadForFirstPerson);
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

	// A slower, larger horizontal sway on top of the head bob so this same
	// synthetic path can also exercise the M3 leg step state machine without
	// a real headset - the bob above is far tighter than any rig's measured
	// step-distance threshold and would never move a foot.
	const float walkAmplitude = 300.0f;
	vec3 walkOffset(std::sin(timeSeconds * 0.8f) * walkAmplitude, 0.0f, 0.0f);

	input.Head.Valid = true;
	input.Head.Position = cameraLocation + headOffset + walkOffset;

	const float reach = 40.0f + std::sin(timeSeconds * 0.75f) * 20.0f;
	input.LeftHandGrip.Valid = true;
	input.LeftHandGrip.Position = cameraLocation + vec3(reach, -25.0f, -10.0f);

	input.RightHandGrip.Valid = true;
	input.RightHandGrip.Position = cameraLocation + vec3(reach, 25.0f, -10.0f);

	// Periodically simulate an airborne spell (jump/fall) so this same debug
	// path can also demonstrate the leg IK's neutral-hang blend without
	// needing a real jump; brief relative to the grounded stretches so the
	// step machine above still gets plenty of grounded time to react to.
	input.SyntheticAirborne = std::fmod(timeSeconds, 8.0f) > 6.5f;

	return input;
}
