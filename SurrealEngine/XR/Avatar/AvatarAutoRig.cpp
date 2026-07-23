
#include "Precomp.h"
#include "AvatarAutoRig.h"
#include "UObject/UMesh.h"
#include "Package/Package.h"
#include "Package/NameString.h"
#include "MurmurHash3/MurmurHash3.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <set>
#include <unordered_map>

namespace
{
	// ---- triangle topology, independent of mesh storage format -------------
	// UMesh's legacy Tris array is only populated for old flat-format meshes.
	// Real ULodMesh/USkeletalMesh assets (every stock UT99 player model) store
	// geometry as Faces/Wedges/Materials instead and leave Tris empty - so
	// topology (vertex adjacency for clustering, and the triangle list for
	// hashing) has to be read from whichever format the mesh actually used.
	// Wedge vertex indices are offset by SpecialVerts and go through
	// ReMapAnimVerts, same as VisibleMesh::DrawLodMeshFace.
	Array<int> ExtractTriangleVertexIndices(UMesh* mesh)
	{
		Array<int> tris;
		if (!mesh->Tris.empty())
		{
			tris.reserve(mesh->Tris.size() * 3);
			for (const MeshTri& tri : mesh->Tris)
			{
				tris.push_back(tri.Indices[0]);
				tris.push_back(tri.Indices[1]);
				tris.push_back(tri.Indices[2]);
			}
			return tris;
		}

		ULodMesh* lodmesh = UObject::TryCast<ULodMesh>(mesh);
		if (!lodmesh)
			return tris;

		tris.reserve(lodmesh->Faces.size() * 3);
		for (const MeshFace& face : lodmesh->Faces)
		{
			int idx[3];
			bool ok = true;
			for (int i = 0; i < 3; i++)
			{
				size_t wedgeIndex = face.Indices[i];
				if (wedgeIndex >= lodmesh->Wedges.size())
				{
					ok = false;
					break;
				}
				size_t vbase = (size_t)lodmesh->Wedges[wedgeIndex].Vertex + lodmesh->SpecialVerts;
				size_t vindex = vbase;
				if (!lodmesh->ReMapAnimVerts.empty())
				{
					if (vbase >= lodmesh->ReMapAnimVerts.size())
					{
						ok = false;
						break;
					}
					vindex = lodmesh->ReMapAnimVerts[vbase];
				}
				idx[i] = (int)vindex;
			}
			if (!ok)
				continue;
			tris.push_back(idx[0]);
			tris.push_back(idx[1]);
			tris.push_back(idx[2]);
		}
		return tris;
	}

	// ---- small self-contained linear algebra helpers -----------------------
	// (Kabsch/Procrustes rigid-fit via Horn's quaternion method, and the
	// symmetric Jacobi eigensolver it needs. Kept local to this file - this
	// is the only place in the engine that needs rigid-transform fitting.)

	struct RigidFit
	{
		quaternion Rotation;
		vec3 Translation = vec3(0.0f);
		float MaxResidual = 0.0f;
	};

	void JacobiEigenSymmetric4(double a[4][4], double eigenvalues[4], double eigenvectors[4][4])
	{
		for (int i = 0; i < 4; i++)
			for (int j = 0; j < 4; j++)
				eigenvectors[i][j] = (i == j) ? 1.0 : 0.0;

		for (int sweep = 0; sweep < 60; sweep++)
		{
			double off = 0.0;
			for (int p = 0; p < 4; p++)
				for (int q = p + 1; q < 4; q++)
					off += a[p][q] * a[p][q];
			if (off < 1e-24)
				break;

			for (int p = 0; p < 3; p++)
			{
				for (int q = p + 1; q < 4; q++)
				{
					if (std::abs(a[p][q]) < 1e-18)
						continue;

					double theta = (a[q][q] - a[p][p]) / (2.0 * a[p][q]);
					double t = (theta >= 0.0 ? 1.0 : -1.0) / (std::abs(theta) + std::sqrt(theta * theta + 1.0));
					double c = 1.0 / std::sqrt(t * t + 1.0);
					double s = t * c;
					double apq = a[p][q];

					a[p][p] -= t * apq;
					a[q][q] += t * apq;
					a[p][q] = 0.0;
					a[q][p] = 0.0;

					for (int i = 0; i < 4; i++)
					{
						if (i != p && i != q)
						{
							double aip = a[i][p], aiq = a[i][q];
							a[i][p] = a[p][i] = c * aip - s * aiq;
							a[i][q] = a[q][i] = s * aip + c * aiq;
						}
					}
					for (int i = 0; i < 4; i++)
					{
						double vip = eigenvectors[i][p], viq = eigenvectors[i][q];
						eigenvectors[i][p] = c * vip - s * viq;
						eigenvectors[i][q] = s * vip + c * viq;
					}
				}
			}
		}

		for (int i = 0; i < 4; i++)
			eigenvalues[i] = a[i][i];
	}

	// Horn 1987 "Closed-form solution of absolute orientation using unit
	// quaternions": finds the rotation R minimizing sum|R*src_i - dst_i|^2
	// for already-centered point sets, via the eigenvector of the largest
	// eigenvalue of a 4x4 matrix built from the cross-covariance of the
	// point sets - avoids needing a general 3x3 SVD implementation.
	bool ComputeRigidFit(const Array<vec3>& src, const Array<vec3>& dst, RigidFit& outFit)
	{
		size_t n = src.size();
		if (n == 0 || dst.size() != n)
			return false;

		vec3 srcCentroid(0.0f), dstCentroid(0.0f);
		for (size_t i = 0; i < n; i++)
		{
			srcCentroid += src[i];
			dstCentroid += dst[i];
		}
		srcCentroid /= (float)n;
		dstCentroid /= (float)n;

		double Sxx = 0, Sxy = 0, Sxz = 0, Syx = 0, Syy = 0, Syz = 0, Szx = 0, Szy = 0, Szz = 0;
		for (size_t i = 0; i < n; i++)
		{
			vec3 p = src[i] - srcCentroid;
			vec3 q = dst[i] - dstCentroid;
			Sxx += (double)p.x * q.x; Sxy += (double)p.x * q.y; Sxz += (double)p.x * q.z;
			Syx += (double)p.y * q.x; Syy += (double)p.y * q.y; Syz += (double)p.y * q.z;
			Szx += (double)p.z * q.x; Szy += (double)p.z * q.y; Szz += (double)p.z * q.z;
		}

		double N[4][4] =
		{
			{ Sxx + Syy + Szz, Syz - Szy,        Szx - Sxz,        Sxy - Syx },
			{ Syz - Szy,       Sxx - Syy - Szz,  Sxy + Syx,        Szx + Sxz },
			{ Szx - Sxz,       Sxy + Syx,        -Sxx + Syy - Szz, Syz + Szy },
			{ Sxy - Syx,       Szx + Sxz,        Syz + Szy,        -Sxx - Syy + Szz }
		};

		double eigenvalues[4];
		double eigenvectors[4][4];
		JacobiEigenSymmetric4(N, eigenvalues, eigenvectors);

		int best = 0;
		for (int i = 1; i < 4; i++)
			if (eigenvalues[i] > eigenvalues[best])
				best = i;

		quaternion q((float)eigenvectors[1][best], (float)eigenvectors[2][best], (float)eigenvectors[3][best], (float)eigenvectors[0][best]);
		q = normalize(q);

		outFit.Rotation = q;
		outFit.Translation = dstCentroid - q * srcCentroid;

		float maxResidual = 0.0f;
		for (size_t i = 0; i < n; i++)
		{
			vec3 predicted = q * src[i] + outFit.Translation;
			float d = length(predicted - dst[i]);
			if (d > maxResidual)
				maxResidual = d;
		}
		outFit.MaxResidual = maxResidual;
		return true;
	}

	// ---- union-find over per-frame vertex indices [0, FrameVerts) ---------

	struct UnionFind
	{
		Array<int> parent;
		explicit UnionFind(int n)
		{
			parent.resize(n);
			for (int i = 0; i < n; i++)
				parent[i] = i;
		}
		int Find(int x)
		{
			while (parent[x] != x)
			{
				parent[x] = parent[parent[x]];
				x = parent[x];
			}
			return x;
		}
		// Union always makes b's root the surviving root - callers rely on this.
		void Union(int a, int b)
		{
			a = Find(a);
			b = Find(b);
			if (a != b)
				parent[a] = b;
		}
	};

	struct ClusterGrowthContext
	{
		UMesh* mesh = nullptr;
		int frameVerts = 0;
		int bindFrame = 0;
		Array<int> sampleFrames;
	};

	bool ClusterRigidFitOK(const ClusterGrowthContext& ctx, const Array<int>& vertexIds, float tolerance)
	{
		if (vertexIds.size() < 1)
			return true;

		Array<vec3> src(vertexIds.size());
		Array<vec3> dst(vertexIds.size());
		int bindOffset = ctx.bindFrame * ctx.frameVerts;

		for (int frame : ctx.sampleFrames)
		{
			if (frame == ctx.bindFrame)
				continue;

			int frameOffset = frame * ctx.frameVerts;
			for (size_t i = 0; i < vertexIds.size(); i++)
			{
				src[i] = ctx.mesh->Verts[bindOffset + vertexIds[i]];
				dst[i] = ctx.mesh->Verts[frameOffset + vertexIds[i]];
			}

			RigidFit fit;
			if (!ComputeRigidFit(src, dst, fit))
				return false;
			if (fit.MaxResidual > tolerance)
				return false;
		}
		return true;
	}

	// Rigid-cluster growing (plan doc "Runtime auto-rig" step 2): greedily
	// merges triangle-adjacent vertex clusters whenever the combined set
	// still admits one rigid transform across every sampled animation frame.
	Array<int> GrowRigidClusters(UMesh* mesh, int bindFrame, const Array<int>& sampleFrames, int clusterCeiling, float tolerance)
	{
		int frameVerts = mesh->FrameVerts;
		UnionFind uf(frameVerts);

		std::set<std::pair<int, int>> edgeSet;
		Array<int> triVerts = ExtractTriangleVertexIndices(mesh);
		for (size_t t = 0; t + 2 < triVerts.size(); t += 3)
		{
			int idx[3] = { triVerts[t], triVerts[t + 1], triVerts[t + 2] };
			for (int i = 0; i < 3; i++)
			{
				int a = idx[i];
				int b = idx[(i + 1) % 3];
				if (a < 0 || a >= frameVerts || b < 0 || b >= frameVerts || a == b)
					continue;
				if (a > b)
					std::swap(a, b);
				edgeSet.insert({ a, b });
			}
		}
		Array<std::pair<int, int>> edges(edgeSet.begin(), edgeSet.end());

		ClusterGrowthContext ctx;
		ctx.mesh = mesh;
		ctx.frameVerts = frameVerts;
		ctx.bindFrame = bindFrame;
		ctx.sampleFrames = sampleFrames;

		int clusterCount = frameVerts;
		std::set<std::pair<int, int>> rejected;

		bool progress = true;
		int passLimit = 80;
		while (progress && clusterCount > clusterCeiling && passLimit-- > 0)
		{
			progress = false;

			std::unordered_map<int, Array<int>> members;
			for (int v = 0; v < frameVerts; v++)
				members[uf.Find(v)].push_back(v);

			for (auto& e : edges)
			{
				int ra = uf.Find(e.first);
				int rb = uf.Find(e.second);
				if (ra == rb)
					continue;

				int key0 = std::min(ra, rb);
				int key1 = std::max(ra, rb);
				if (rejected.count({ key0, key1 }))
					continue;

				auto itA = members.find(ra);
				auto itB = members.find(rb);
				if (itA == members.end() || itB == members.end())
					continue;

				Array<int> merged;
				merged.reserve(itA->second.size() + itB->second.size());
				for (int v : itA->second) merged.push_back(v);
				for (int v : itB->second) merged.push_back(v);

				if (ClusterRigidFitOK(ctx, merged, tolerance))
				{
					uf.Union(ra, rb); // rb survives, per UnionFind::Union's contract
					members.erase(ra);
					members[rb] = std::move(merged);
					clusterCount--;
					progress = true;
					if (clusterCount <= clusterCeiling)
						break;
				}
				else
				{
					rejected.insert({ key0, key1 });
				}
			}
		}

		Array<int> vertexCluster(frameVerts);
		for (int v = 0; v < frameVerts; v++)
			vertexCluster[v] = uf.Find(v);
		return vertexCluster;
	}

	// ---- bind-pose topology labeling (plan doc step 3) ---------------------

	struct BindStats
	{
		float minUp = 0.0f, maxUp = 1.0f;
		float centerLateral = 0.0f, lateralExtent = 1.0f;
		int lateralAxis = 0; // 0 = x, 1 = y (in oriented space; z is always "up")
	};

	// Reused by both the primary (per-cluster) and fallback (per-vertex)
	// tiers - see plan doc step 6 ("same canonical bands").
	struct RoleTarget
	{
		AvatarJointRole Role;
		float UpFraction;
	};

	const RoleTarget CoreTargets[] =
	{
		{ AvatarJointRole::Pelvis, 0.52f },
		{ AvatarJointRole::Spine,  0.62f },
		{ AvatarJointRole::Chest,  0.72f },
		{ AvatarJointRole::Neck,   0.85f },
		{ AvatarJointRole::Head,   0.95f },
	};

	const RoleTarget LeftArmTargets[] =
	{
		{ AvatarJointRole::LeftShoulder, 0.80f },
		{ AvatarJointRole::LeftUpperArm, 0.66f },
		{ AvatarJointRole::LeftForearm,  0.55f },
		{ AvatarJointRole::LeftHand,     0.45f },
	};
	const RoleTarget RightArmTargets[] =
	{
		{ AvatarJointRole::RightShoulder, 0.80f },
		{ AvatarJointRole::RightUpperArm, 0.66f },
		{ AvatarJointRole::RightForearm,  0.55f },
		{ AvatarJointRole::RightHand,     0.45f },
	};
	const RoleTarget LeftLegTargets[] =
	{
		{ AvatarJointRole::LeftThigh, 0.36f },
		{ AvatarJointRole::LeftCalf,  0.15f },
		{ AvatarJointRole::LeftFoot,  0.02f },
	};
	const RoleTarget RightLegTargets[] =
	{
		{ AvatarJointRole::RightThigh, 0.36f },
		{ AvatarJointRole::RightCalf,  0.15f },
		{ AvatarJointRole::RightFoot,  0.02f },
	};

	// A candidate cluster/vertex to be matched against one bucket of role targets.
	struct LabelCandidate
	{
		int Index = 0;      // cluster index, or vertex index for the fallback tier
		float UpFraction = 0.0f;
		float MaxUpFraction = 0.0f; // highest point reached by this cluster (arm/leg split)
		float LateralOffset = 0.0f; // signed, relative to BindStats::centerLateral
		int VertexCount = 1;
	};

	float LateralOf(const vec3& oriented, const BindStats& stats)
	{
		return (stats.lateralAxis == 0 ? oriented.x : oriented.y) - stats.centerLateral;
	}

	float UpFractionOf(const vec3& oriented, const BindStats& stats)
	{
		float span = std::max(1.0f, stats.maxUp - stats.minUp);
		return clamp((oriented.z - stats.minUp) / span, 0.0f, 1.0f);
	}

	// Greedily assigns candidates in `bucket` to role targets, always
	// resolving the single globally-closest (target, candidate) pair first
	// so one cluster is never claimed by two roles while a worse-but-unique
	// match exists for one of them. Fills outBestCandidateForTarget[target]
	// with the winning candidate's position in `bucket`, or -1 if unfilled
	// (more targets than candidates in this bucket).
	void AssignBucketRoles(const Array<LabelCandidate>& bucket, const RoleTarget* targets, int targetCount, Array<int>& outBestCandidateForTarget)
	{
		outBestCandidateForTarget.assign((size_t)targetCount, -1);
		if (bucket.empty())
			return;

		Array<bool> candidateUsed(bucket.size(), false);
		Array<bool> targetFilled(targetCount, false);
		int rounds = std::min((int)bucket.size(), targetCount);

		for (int iter = 0; iter < rounds; iter++)
		{
			int bestTarget = -1;
			int bestCandidate = -1;
			float bestDist = 1e30f;
			for (int t = 0; t < targetCount; t++)
			{
				if (targetFilled[t])
					continue;
				for (size_t c = 0; c < bucket.size(); c++)
				{
					if (candidateUsed[c])
						continue;
					float d = std::abs(bucket[c].UpFraction - targets[t].UpFraction);
					if (d < bestDist)
					{
						bestDist = d;
						bestTarget = t;
						bestCandidate = (int)c;
					}
				}
			}
			if (bestTarget < 0)
				break;
			outBestCandidateForTarget[bestTarget] = bestCandidate;
			targetFilled[bestTarget] = true;
			candidateUsed[bestCandidate] = true;
		}
	}

	int NearestTargetIndex(float upFraction, const RoleTarget* targets, int targetCount)
	{
		int best = 0;
		float bestDist = std::abs(upFraction - targets[0].UpFraction);
		for (int t = 1; t < targetCount; t++)
		{
			float d = std::abs(upFraction - targets[t].UpFraction);
			if (d < bestDist)
			{
				bestDist = d;
				best = t;
			}
		}
		return best;
	}

	// ---- per-chain completeness repair ------------------------------------
	// A limb chain can legitimately collapse to fewer rigid clusters than it
	// has roles for, when that limb barely articulates across the sampled
	// animation frames (see AvatarAutoRig.h remarks / plan doc step 2) - the
	// rigidity test is correct, there just isn't enough distinct motion to
	// split the limb into sub-joints. AssignBucketRoles then hands the one
	// surviving cluster to whichever single role's target height is closest,
	// leaving the rest of that chain's roles unfilled. This repairs that by
	// re-slicing the chain's own donor vertices along the limb's bind-pose
	// axis into even bands - the same "nearest band by distance" idea the
	// whole-mesh fallback tier (buildFallbackBands) uses, just scoped to one
	// deficient chain's geometry instead of the whole mesh.
	struct ChainRepairSpec
	{
		AvatarJointRole Roles[3]; // proximal -> distal
		AvatarJointRole PreserveRole; // left alone if already assigned (Shoulder); Unknown if none
		AvatarJointRole AnchorRole; // proximal parent joint this chain hangs off (Chest or Pelvis)
		const Array<LabelCandidate>* Bucket;
	};

	void RepairDeficientChain(const ChainRepairSpec& spec, Array<std::pair<int, Array<int>>>& clusters,
		Array<int>& roleToClusterIndex, const Array<vec3>& bindVertices, std::string& diagnostic)
	{
		int filled = 0;
		for (AvatarJointRole role : spec.Roles)
			if (roleToClusterIndex[(size_t)role] >= 0)
				filled++;
		if (filled == 3)
			return; // chain already has a distinct cluster for every required role

		const char* chainName = AvatarJointRoleName(spec.Roles[0]);

		// Donor vertices: every cluster in this chain's bucket except the one
		// already holding the optional preserve role (e.g. a genuinely
		// distinct shoulder cluster is left as-is; everything else in the
		// bucket - including whatever currently holds any of the 3 required
		// roles - gets re-sliced fresh so the chain ends up consistent).
		Array<int> donorClusterIdx;
		for (const LabelCandidate& cand : *spec.Bucket)
		{
			if (spec.PreserveRole != AvatarJointRole::Unknown && roleToClusterIndex[(size_t)spec.PreserveRole] == cand.Index)
				continue;
			donorClusterIdx.push_back(cand.Index);
		}
		if (donorClusterIdx.empty())
		{
			diagnostic += std::string("; ") + chainName + " chain incomplete: no donor geometry found";
			return;
		}

		Array<int> donorVerts;
		for (int idx : donorClusterIdx)
			for (int v : clusters[idx].second)
				donorVerts.push_back(v);
		if ((int)donorVerts.size() < 3)
		{
			diagnostic += std::string("; ") + chainName + " chain incomplete: only " +
				std::to_string(donorVerts.size()) + " donor vertex(es), too few to subdivide into 3 roles";
			return;
		}

		int anchorClusterIdx = roleToClusterIndex[(size_t)spec.AnchorRole];
		if (anchorClusterIdx < 0)
		{
			diagnostic += std::string("; ") + chainName + " chain incomplete: no anchor joint (" + AvatarJointRoleName(spec.AnchorRole) + ") found";
			return;
		}

		vec3 anchorSum(0.0f);
		for (int v : clusters[anchorClusterIdx].second)
			anchorSum += bindVertices[v];
		vec3 anchorPos = anchorSum / (float)clusters[anchorClusterIdx].second.size();

		int farthestV = -1;
		float farthestDist = -1.0f;
		for (int v : donorVerts)
		{
			float d = length(bindVertices[v] - anchorPos);
			if (d > farthestDist) { farthestDist = d; farthestV = v; }
		}
		if (farthestV < 0 || farthestDist <= 1e-3f)
		{
			diagnostic += std::string("; ") + chainName + " chain incomplete: degenerate geometry, cannot subdivide";
			return;
		}
		vec3 axis = (bindVertices[farthestV] - anchorPos) / farthestDist;

		// Equal-count bands (sorted by projection along axis), not equal-range
		// - the donor vertex distribution along a limb is often lopsided (e.g.
		// far more hand/foot geometry than upper-limb geometry), and an
		// equal-range split can starve a proximal band to zero members while
		// an equal-count split always gives every band its share as long as
		// there are at least 3 donor vertices (checked above).
		Array<int> sortedVerts = donorVerts;
		std::sort(sortedVerts.begin(), sortedVerts.end(), [&](int a, int b)
			{
				return dot(bindVertices[a] - anchorPos, axis) < dot(bindVertices[b] - anchorPos, axis);
			});

		Array<int> bandVerts[3];
		size_t n = sortedVerts.size();
		size_t cut1 = n / 3;
		size_t cut2 = (n * 2) / 3;
		for (size_t i = 0; i < n; i++)
		{
			int band = (i < cut1) ? 0 : (i < cut2 ? 1 : 2);
			bandVerts[band].push_back(sortedVerts[i]);
		}

		// Donor clusters are fully absorbed into the fresh bands - clear them
		// so the later "home leftover clusters to nearest joint" pass (which
		// operates at whole-cluster granularity and would otherwise dump an
		// entire stale cluster onto one joint again) has nothing left to do
		// for them.
		for (int idx : donorClusterIdx)
			clusters[idx].second.clear();

		for (int i = 0; i < 3; i++)
		{
			int newClusterIdx = (int)clusters.size();
			clusters.push_back({ newClusterIdx, std::move(bandVerts[i]) });
			roleToClusterIndex[(size_t)spec.Roles[i]] = newClusterIdx;
		}
	}
}

const char* AvatarJointRoleName(AvatarJointRole role)
{
	switch (role)
	{
	case AvatarJointRole::Pelvis: return "Pelvis";
	case AvatarJointRole::Spine: return "Spine";
	case AvatarJointRole::Chest: return "Chest";
	case AvatarJointRole::Neck: return "Neck";
	case AvatarJointRole::Head: return "Head";
	case AvatarJointRole::LeftShoulder: return "LeftShoulder";
	case AvatarJointRole::LeftUpperArm: return "LeftUpperArm";
	case AvatarJointRole::LeftForearm: return "LeftForearm";
	case AvatarJointRole::LeftHand: return "LeftHand";
	case AvatarJointRole::RightShoulder: return "RightShoulder";
	case AvatarJointRole::RightUpperArm: return "RightUpperArm";
	case AvatarJointRole::RightForearm: return "RightForearm";
	case AvatarJointRole::RightHand: return "RightHand";
	case AvatarJointRole::LeftThigh: return "LeftThigh";
	case AvatarJointRole::LeftCalf: return "LeftCalf";
	case AvatarJointRole::LeftFoot: return "LeftFoot";
	case AvatarJointRole::RightThigh: return "RightThigh";
	case AvatarJointRole::RightCalf: return "RightCalf";
	case AvatarJointRole::RightFoot: return "RightFoot";
	default: return "Unknown";
	}
}

// Fixed biped hierarchy (plan doc step 4) - topology is a direct function of
// the role, never inferred from motion.
int AvatarJointRoleParent(AvatarJointRole role)
{
	switch (role)
	{
	case AvatarJointRole::Pelvis: return -1;
	case AvatarJointRole::Spine: return (int)AvatarJointRole::Pelvis;
	case AvatarJointRole::Chest: return (int)AvatarJointRole::Spine;
	case AvatarJointRole::Neck: return (int)AvatarJointRole::Chest;
	case AvatarJointRole::Head: return (int)AvatarJointRole::Neck;
	case AvatarJointRole::LeftShoulder: return (int)AvatarJointRole::Chest;
	case AvatarJointRole::LeftUpperArm: return (int)AvatarJointRole::LeftShoulder;
	case AvatarJointRole::LeftForearm: return (int)AvatarJointRole::LeftUpperArm;
	case AvatarJointRole::LeftHand: return (int)AvatarJointRole::LeftForearm;
	case AvatarJointRole::RightShoulder: return (int)AvatarJointRole::Chest;
	case AvatarJointRole::RightUpperArm: return (int)AvatarJointRole::RightShoulder;
	case AvatarJointRole::RightForearm: return (int)AvatarJointRole::RightUpperArm;
	case AvatarJointRole::RightHand: return (int)AvatarJointRole::RightForearm;
	case AvatarJointRole::LeftThigh: return (int)AvatarJointRole::Pelvis;
	case AvatarJointRole::LeftCalf: return (int)AvatarJointRole::LeftThigh;
	case AvatarJointRole::LeftFoot: return (int)AvatarJointRole::LeftCalf;
	case AvatarJointRole::RightThigh: return (int)AvatarJointRole::Pelvis;
	case AvatarJointRole::RightCalf: return (int)AvatarJointRole::RightThigh;
	case AvatarJointRole::RightFoot: return (int)AvatarJointRole::RightCalf;
	default: return -1;
	}
}

uint64_t AvatarAutoRig::ComputeMeshContentHash(UMesh* mesh)
{
	if (!mesh)
		return 0;

	uint64_t hash[2] = { 0, 0 };
	uint32_t seed = 0x53455641u; // 'SEVA'

	// Hash frame 0's positions (bind-relevant data) plus the triangle list,
	// rather than every animation frame, to keep this cheap - a different
	// bind frame or a modified topology will still change it. Fields are
	// appended individually (not memcpy'd as structs) so compiler padding
	// bytes never leak uninitialized data into the hash.
	std::vector<uint8_t> buffer;
	auto append = [&buffer](const void* data, size_t size)
	{
		const uint8_t* p = (const uint8_t*)data;
		buffer.insert(buffer.end(), p, p + size);
	};

	size_t vertCount = std::min<size_t>(mesh->Verts.size(), (size_t)mesh->FrameVerts);
	Array<int> triVerts = ExtractTriangleVertexIndices(mesh);
	buffer.reserve(vertCount * sizeof(vec3) + triVerts.size() * sizeof(int) + sizeof(int) * 2);

	for (size_t i = 0; i < vertCount; i++)
		append(&mesh->Verts[i], sizeof(vec3));

	for (int v : triVerts)
		append(&v, sizeof(v));

	append(&mesh->FrameVerts, sizeof(int));
	append(&mesh->AnimFrames, sizeof(int));

	MurmurHash3_x64_128(buffer.data(), (int)buffer.size(), seed, hash);
	return hash[0];
}

static int PickBindFrame(UMesh* mesh)
{
	// Prefer an Idle-ish sequence's first frame; otherwise just frame 0.
	static const char* idleNames[] = { "Idle", "Idle2", "WaitPain", "Rest" };
	for (const char* name : idleNames)
	{
		MeshAnimSeq* seq = mesh->GetSequence(NameString(name));
		if (seq && seq->Name == name && seq->NumFrames > 0)
			return seq->StartFrame;
	}
	return 0;
}

static Array<int> PickSampleFrames(UMesh* mesh, int bindFrame)
{
	static const char* preferredGroups[] = { "Walk", "Run", "Crouch", "CrouchWalk", "Turn", "Fire", "AltFire" };

	std::set<int> frames;
	frames.insert(bindFrame);

	auto sampleSequence = [&](const MeshAnimSeq& seq)
	{
		if (seq.NumFrames <= 0)
			return;
		const int maxSamplesPerSeq = 4;
		int samples = std::min(maxSamplesPerSeq, seq.NumFrames);
		for (int s = 0; s < samples; s++)
		{
			int frameInSeq = (samples > 1) ? (s * (seq.NumFrames - 1)) / (samples - 1) : 0;
			frames.insert(seq.StartFrame + frameInSeq);
		}
	};

	bool matchedAny = false;
	for (const char* group : preferredGroups)
	{
		for (const MeshAnimSeq& seq : mesh->AnimSeqs)
		{
			const std::string& n = seq.Name.ToString();
			if (n.size() >= std::string(group).size() &&
				std::equal(std::string(group).begin(), std::string(group).end(), n.begin(),
					[](char a, char b) { return std::tolower((unsigned char)a) == std::tolower((unsigned char)b); }))
			{
				sampleSequence(seq);
				matchedAny = true;
			}
		}
	}

	if (!matchedAny)
	{
		// Fall back to whatever sequences exist, capped so extraction stays cheap.
		int seqBudget = 6;
		for (const MeshAnimSeq& seq : mesh->AnimSeqs)
		{
			if (seqBudget-- <= 0)
				break;
			sampleSequence(seq);
		}
	}

	// Bound total sample count - the rigid-fit test cost scales with this.
	Array<int> result(frames.begin(), frames.end());
	const size_t maxTotalSamples = 20;
	if (result.size() > maxTotalSamples)
	{
		Array<int> reduced;
		reduced.push_back(bindFrame);
		float step = (float)result.size() / (float)(maxTotalSamples - 1);
		for (size_t i = 0; i < maxTotalSamples - 1; i++)
		{
			size_t idx = std::min((size_t)(i * step), result.size() - 1);
			if (result[idx] != bindFrame)
				reduced.push_back(result[idx]);
		}
		result = std::move(reduced);
	}

	return result;
}

AvatarRig AvatarAutoRig::Build(UMesh* mesh)
{
	AvatarRig rig;
	if (!mesh)
	{
		rig.Diagnostic = "not riggable: no mesh";
		return rig;
	}

	rig.MeshIdentity = (mesh->package ? mesh->package->GetPackageName().ToString() + "." : std::string()) + mesh->Name.ToString();
	rig.FrameVerts = mesh->FrameVerts;
	rig.MeshContentHash = ComputeMeshContentHash(mesh);

	if (mesh->FrameVerts <= 0 || mesh->AnimFrames <= 0 ||
		mesh->Verts.size() < (size_t)mesh->FrameVerts || mesh->Normals.size() < (size_t)mesh->FrameVerts ||
		ExtractTriangleVertexIndices(mesh).empty())
	{
		rig.Diagnostic = "not riggable: mesh has no usable vertex-anim data";
		return rig;
	}

	int frameVerts = mesh->FrameVerts;
	int bindFrame = PickBindFrame(mesh);
	if (bindFrame < 0 || bindFrame >= mesh->AnimFrames)
		bindFrame = 0;
	int bindOffset = bindFrame * frameVerts;

	rig.BindFrame = bindFrame;
	rig.BindVertices.resize(frameVerts);
	rig.BindNormals.resize(frameVerts);
	for (int i = 0; i < frameVerts; i++)
	{
		rig.BindVertices[i] = mesh->Verts[bindOffset + i];
		rig.BindNormals[i] = mesh->Normals[bindOffset + i];
	}

	Array<int> sampleFrames = PickSampleFrames(mesh, bindFrame);

	const int clusterCeiling = 24;
	const float rigidTolerance = 2.0f; // mesh-local units; see AvatarAutoRig.h remarks

	bool fallback = false;
	std::string fallbackReason;
	Array<int> vertexCluster; // per-vertex cluster root id, only valid if !fallback

	if (sampleFrames.size() < 2)
	{
		fallback = true;
		fallbackReason = "fewer than 2 usable animation-pose samples";
	}
	else
	{
		vertexCluster = GrowRigidClusters(mesh, bindFrame, sampleFrames, clusterCeiling, rigidTolerance);
	}

	// oriented-space bind stats, shared by both tiers (see comment on BindStats).
	mat3 orient(mesh->meshToObject);
	Array<vec3> orientedBind(frameVerts);
	for (int i = 0; i < frameVerts; i++)
		orientedBind[i] = orient * rig.BindVertices[i];

	BindStats stats;
	stats.minUp = 1e30f;
	stats.maxUp = -1e30f;
	float minX = 1e30f, maxX = -1e30f, minY = 1e30f, maxY = -1e30f;
	for (const vec3& p : orientedBind)
	{
		stats.minUp = std::min(stats.minUp, p.z);
		stats.maxUp = std::max(stats.maxUp, p.z);
		minX = std::min(minX, p.x); maxX = std::max(maxX, p.x);
		minY = std::min(minY, p.y); maxY = std::max(maxY, p.y);
	}
	float extentX = maxX - minX;
	float extentY = maxY - minY;
	if (extentX >= extentY)
	{
		stats.lateralAxis = 0;
		stats.centerLateral = (minX + maxX) * 0.5f;
		stats.lateralExtent = std::max(1.0f, extentX * 0.5f);
	}
	else
	{
		stats.lateralAxis = 1;
		stats.centerLateral = (minY + maxY) * 0.5f;
		stats.lateralExtent = std::max(1.0f, extentY * 0.5f);
	}

	// Build per-vertex joint assignment (uint8_t index into rig.Joints, filled below).
	rig.VertexJoint.resize(frameVerts, 0);

	auto buildFromClusters = [&]() -> bool
	{
		// Collect clusters (root id -> member vertex list), enforce the ceiling
		// by keeping the largest N and re-homing stragglers to their nearest
		// surviving cluster centroid.
		std::unordered_map<int, Array<int>> clusterMembers;
		for (int v = 0; v < frameVerts; v++)
			clusterMembers[vertexCluster[v]].push_back(v);

		Array<std::pair<int, Array<int>>> clusters(clusterMembers.begin(), clusterMembers.end());
		std::sort(clusters.begin(), clusters.end(), [](auto& a, auto& b) { return a.second.size() > b.second.size(); });

		if ((int)clusters.size() > clusterCeiling)
		{
			// Compute centroids for the kept clusters, then re-home stragglers.
			Array<vec3> keptCentroid(clusterCeiling);
			for (int c = 0; c < clusterCeiling; c++)
			{
				vec3 sum(0.0f);
				for (int v : clusters[c].second)
					sum += orientedBind[v];
				keptCentroid[c] = sum / (float)clusters[c].second.size();
			}
			for (size_t c = clusterCeiling; c < clusters.size(); c++)
			{
				for (int v : clusters[c].second)
				{
					int best = 0;
					float bestDist = length(orientedBind[v] - keptCentroid[0]);
					for (int k = 1; k < clusterCeiling; k++)
					{
						float d = length(orientedBind[v] - keptCentroid[k]);
						if (d < bestDist) { bestDist = d; best = k; }
					}
					clusters[best].second.push_back(v);
				}
			}
			clusters.resize(clusterCeiling);
		}

		if (clusters.size() < 4)
			return false; // degenerate mesh - not enough distinct rigid parts

		// Build labeling candidates.
		Array<LabelCandidate> allCandidates(clusters.size());
		for (size_t c = 0; c < clusters.size(); c++)
		{
			vec3 sum(0.0f);
			float maxUp = -1e30f;
			for (int v : clusters[c].second)
			{
				sum += orientedBind[v];
				maxUp = std::max(maxUp, UpFractionOf(orientedBind[v], stats));
			}
			vec3 centroid = sum / (float)clusters[c].second.size();

			LabelCandidate cand;
			cand.Index = (int)c;
			cand.UpFraction = UpFractionOf(centroid, stats);
			cand.MaxUpFraction = maxUp;
			cand.LateralOffset = LateralOf(centroid, stats);
			cand.VertexCount = (int)clusters[c].second.size();
			allCandidates[c] = cand;
		}

		const float coreLateralFrac = 0.18f;
		const float armReachThreshold = 0.42f; // "reaches up into torso height" cutoff for arm vs leg

		Array<LabelCandidate> coreC, leftArmC, rightArmC, leftLegC, rightLegC;
		for (const LabelCandidate& cand : allCandidates)
		{
			float lateralFrac = std::abs(cand.LateralOffset) / stats.lateralExtent;
			if (lateralFrac < coreLateralFrac)
			{
				coreC.push_back(cand);
				continue;
			}
			bool isLeft = cand.LateralOffset < 0.0f; // convention: negative lateral = "left" (see AvatarAutoRig.h)
			bool isArm = cand.MaxUpFraction >= armReachThreshold;
			if (isArm)
				(isLeft ? leftArmC : rightArmC).push_back(cand);
			else
				(isLeft ? leftLegC : rightLegC).push_back(cand);
		}

		struct BucketAssignment
		{
			const Array<LabelCandidate>* bucket;
			const RoleTarget* targets;
			int targetCount;
		};
		BucketAssignment buckets[] =
		{
			{ &coreC,     CoreTargets,     (int)(sizeof(CoreTargets) / sizeof(CoreTargets[0])) },
			{ &leftArmC,  LeftArmTargets,  (int)(sizeof(LeftArmTargets) / sizeof(LeftArmTargets[0])) },
			{ &rightArmC, RightArmTargets, (int)(sizeof(RightArmTargets) / sizeof(RightArmTargets[0])) },
			{ &leftLegC,  LeftLegTargets,  (int)(sizeof(LeftLegTargets) / sizeof(LeftLegTargets[0])) },
			{ &rightLegC, RightLegTargets, (int)(sizeof(RightLegTargets) / sizeof(RightLegTargets[0])) },
		};

		// role -> cluster index (into `clusters`), or -1 if this mesh has no candidate for it.
		Array<int> roleToClusterIndex((size_t)AvatarJointRole::Count, -1);

		for (auto& b : buckets)
		{
			Array<int> bestForTarget;
			AssignBucketRoles(*b.bucket, b.targets, b.targetCount, bestForTarget);
			for (int t = 0; t < b.targetCount; t++)
			{
				if (bestForTarget[t] < 0)
					continue;
				const LabelCandidate& winner = (*b.bucket)[bestForTarget[t]];
				roleToClusterIndex[(size_t)b.targets[t].Role] = winner.Index;
			}
		}

		// A limb whose motion data didn't distinguish sub-joints collapses to
		// one role instead of a full chain (see RepairDeficientChain above) -
		// fix that per chain before deciding whether this mesh is riggable at
		// all, so a single stiff limb doesn't force the whole mesh to the
		// whole-mesh fallback tier.
		ChainRepairSpec chainRepairs[] =
		{
			{ { AvatarJointRole::LeftUpperArm, AvatarJointRole::LeftForearm, AvatarJointRole::LeftHand },
				AvatarJointRole::LeftShoulder, AvatarJointRole::Chest, &leftArmC },
			{ { AvatarJointRole::RightUpperArm, AvatarJointRole::RightForearm, AvatarJointRole::RightHand },
				AvatarJointRole::RightShoulder, AvatarJointRole::Chest, &rightArmC },
			{ { AvatarJointRole::LeftThigh, AvatarJointRole::LeftCalf, AvatarJointRole::LeftFoot },
				AvatarJointRole::Unknown, AvatarJointRole::Pelvis, &leftLegC },
			{ { AvatarJointRole::RightThigh, AvatarJointRole::RightCalf, AvatarJointRole::RightFoot },
				AvatarJointRole::Unknown, AvatarJointRole::Pelvis, &rightLegC },
		};
		for (const ChainRepairSpec& spec : chainRepairs)
			RepairDeficientChain(spec, clusters, roleToClusterIndex, rig.BindVertices, rig.Diagnostic);

		int filledRoles = 0;
		for (int r : roleToClusterIndex)
			if (r >= 0)
				filledRoles++;

		if (filledRoles < 8)
			return false; // too few recognizable biped parts - let the fallback tier handle it

		// Emit joints in a stable role order, remembering cluster->joint index.
		Array<int> clusterToJoint(clusters.size(), -1);
		for (size_t roleIdx = 0; roleIdx < roleToClusterIndex.size(); roleIdx++)
		{
			int clusterIdx = roleToClusterIndex[roleIdx];
			if (clusterIdx < 0)
				continue;

			AvatarJointRole role = (AvatarJointRole)roleIdx;
			AvatarJoint joint;
			joint.Role = role;
			vec3 sum(0.0f);
			for (int v : clusters[clusterIdx].second)
				sum += rig.BindVertices[v];
			joint.BindOrigin = sum / (float)clusters[clusterIdx].second.size();
			joint.VertexCount = 0; // filled below once every vertex (including re-homed ones) is assigned

			clusterToJoint[clusterIdx] = (int)rig.Joints.size();
			rig.Joints.push_back(joint);
		}

		// Wire up parent indices now that every role's joint index is known.
		Array<int> roleToJointIndex((size_t)AvatarJointRole::Count, -1);
		for (size_t j = 0; j < rig.Joints.size(); j++)
			roleToJointIndex[(size_t)rig.Joints[j].Role] = (int)j;
		for (AvatarJoint& joint : rig.Joints)
		{
			int parentRole = AvatarJointRoleParent(joint.Role);
			joint.Parent = (parentRole >= 0) ? roleToJointIndex[parentRole] : -1;
		}

		// Assign every cluster (including ones that lost the role contest) to
		// its nearest surviving joint by bind-pose centroid distance, then
		// stamp VertexJoint for every vertex in that cluster.
		Array<vec3> jointCentroid(rig.Joints.size());
		for (size_t j = 0; j < rig.Joints.size(); j++)
			jointCentroid[j] = rig.Joints[j].BindOrigin;

		for (size_t c = 0; c < clusters.size(); c++)
		{
			int joint = clusterToJoint[c];
			if (joint < 0)
			{
				vec3 sum(0.0f);
				for (int v : clusters[c].second)
					sum += rig.BindVertices[v];
				vec3 centroid = sum / (float)clusters[c].second.size();

				int best = 0;
				float bestDist = length(centroid - jointCentroid[0]);
				for (size_t j = 1; j < jointCentroid.size(); j++)
				{
					float d = length(centroid - jointCentroid[j]);
					if (d < bestDist) { bestDist = d; best = (int)j; }
				}
				joint = best;
			}

			rig.Joints[joint].VertexCount += (int)clusters[c].second.size();
			for (int v : clusters[c].second)
				rig.VertexJoint[v] = (uint8_t)joint;
		}

		return true;
	};

	auto buildFallbackBands = [&]()
	{
		rig.Joints.clear();
		struct RoleGroup { const RoleTarget* Targets; int Count; };
		const RoleGroup allGroups[] =
		{
			{ CoreTargets,     (int)(sizeof(CoreTargets) / sizeof(CoreTargets[0])) },
			{ LeftArmTargets,  (int)(sizeof(LeftArmTargets) / sizeof(LeftArmTargets[0])) },
			{ RightArmTargets, (int)(sizeof(RightArmTargets) / sizeof(RightArmTargets[0])) },
			{ LeftLegTargets,  (int)(sizeof(LeftLegTargets) / sizeof(LeftLegTargets[0])) },
			{ RightLegTargets, (int)(sizeof(RightLegTargets) / sizeof(RightLegTargets[0])) },
		};
		for (const RoleGroup& group : allGroups)
		{
			for (int i = 0; i < group.Count; i++)
			{
				AvatarJoint joint;
				joint.Role = group.Targets[i].Role;
				rig.Joints.push_back(joint);
			}
		}

		Array<int> roleToJointIndex((size_t)AvatarJointRole::Count, -1);
		for (size_t j = 0; j < rig.Joints.size(); j++)
			roleToJointIndex[(size_t)rig.Joints[j].Role] = (int)j;
		for (AvatarJoint& joint : rig.Joints)
		{
			int parentRole = AvatarJointRoleParent(joint.Role);
			joint.Parent = (parentRole >= 0) ? roleToJointIndex[parentRole] : -1;
		}

		const float coreLateralFrac = 0.18f;
		const float armReachThreshold = 0.42f;

		Array<vec3> sumByJoint(rig.Joints.size(), vec3(0.0f));
		Array<int> countByJoint(rig.Joints.size(), 0);

		for (int v = 0; v < frameVerts; v++)
		{
			const vec3& oriented = orientedBind[v];
			float lateralFrac = LateralOf(oriented, stats) / stats.lateralExtent;
			float upFrac = UpFractionOf(oriented, stats);

			int roleIdx;
			if (std::abs(lateralFrac) < coreLateralFrac)
			{
				roleIdx = (int)CoreTargets[NearestTargetIndex(upFrac, CoreTargets, (int)(sizeof(CoreTargets) / sizeof(CoreTargets[0])))].Role;
			}
			else
			{
				bool isLeft = lateralFrac < 0.0f;
				// Fallback tier has no motion data to know each vertex's own
				// cluster reach, so approximate arm-vs-leg by height alone.
				bool isArm = upFrac >= armReachThreshold;
				const RoleTarget* targets = isArm ? (isLeft ? LeftArmTargets : RightArmTargets) : (isLeft ? LeftLegTargets : RightLegTargets);
				int targetCount = isArm ? (int)(sizeof(LeftArmTargets) / sizeof(LeftArmTargets[0])) : (int)(sizeof(LeftLegTargets) / sizeof(LeftLegTargets[0]));
				roleIdx = (int)targets[NearestTargetIndex(upFrac, targets, targetCount)].Role;
			}

			int joint = roleToJointIndex[roleIdx];
			rig.VertexJoint[v] = (uint8_t)joint;
			sumByJoint[joint] += rig.BindVertices[v];
			countByJoint[joint]++;
		}

		for (size_t j = 0; j < rig.Joints.size(); j++)
		{
			rig.Joints[j].VertexCount = countByJoint[j];
			rig.Joints[j].BindOrigin = countByJoint[j] > 0 ? sumByJoint[j] / (float)countByJoint[j] : vec3(0.0f);
		}
	};

	if (fallback || !buildFromClusters())
	{
		rig.UsedFallbackTier = true;
		if (fallbackReason.empty())
			fallbackReason = "rigid-cluster labeling did not find a recognizable biped shape";
		rig.Diagnostic = "fallback tier: " + fallbackReason;
		buildFallbackBands();
	}

	rig.Valid = !rig.Joints.empty();
	if (!rig.Valid)
		rig.Diagnostic = "not riggable: no joints could be assigned";

	return rig;
}
