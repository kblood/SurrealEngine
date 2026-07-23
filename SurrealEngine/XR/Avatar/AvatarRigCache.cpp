
#include "Precomp.h"
#include "AvatarRigCache.h"
#include "UObject/UMesh.h"
#include "Package/Package.h"
#include "Utils/File.h"
#include "Utils/Logger.h"

#include <sstream>

std::unordered_map<std::string, AvatarRig> AvatarRigCache::InMemoryCache;

namespace
{
	const uint32_t CacheMagic = 0x47525641u; // "AVRG"
	// v2: AvatarAutoRig gained per-chain completeness repair, which can
	// change the joint layout for a mesh whose content hash is unchanged -
	// bump so any v1 cache file is rebuilt instead of misread.
	const uint32_t CacheVersion = 2;

	std::string SanitizeForFilename(const std::string& s)
	{
		std::string out;
		out.reserve(s.size());
		for (char c : s)
		{
			bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
			out.push_back(ok ? c : '_');
		}
		return out;
	}

	void WriteString(std::shared_ptr<File>& file, const std::string& s)
	{
		uint32_t len = (uint32_t)s.size();
		file->write(&len, sizeof(len));
		if (len > 0)
			file->write(s.data(), len);
	}

	std::string ReadString(std::shared_ptr<File>& file)
	{
		uint32_t len = file->read_uint32();
		std::string s;
		if (len > 0)
		{
			s.resize(len);
			file->read(&s[0], len);
		}
		return s;
	}

	void WriteFloat(std::shared_ptr<File>& file, float f)
	{
		file->write(&f, sizeof(float));
	}

	float ReadFloat(std::shared_ptr<File>& file)
	{
		float f = 0.0f;
		file->read(&f, sizeof(float));
		return f;
	}

	void WriteVec3(std::shared_ptr<File>& file, const vec3& v)
	{
		WriteFloat(file, v.x);
		WriteFloat(file, v.y);
		WriteFloat(file, v.z);
	}

	vec3 ReadVec3(std::shared_ptr<File>& file)
	{
		vec3 v;
		v.x = ReadFloat(file);
		v.y = ReadFloat(file);
		v.z = ReadFloat(file);
		return v;
	}
}

std::string AvatarRigCache::CacheDirectory()
{
	fs::path dir = Directory::localAppData() / "SurrealEngine" / "AvatarRigCache";
	return dir.string();
}

std::string AvatarRigCache::CacheFilePath(const std::string& meshIdentity, uint64_t contentHash)
{
	std::ostringstream name;
	name << SanitizeForFilename(meshIdentity) << "_" << std::hex << contentHash << ".avatarrig";

	fs::path dir = CacheDirectory();
	return (dir / name.str()).string();
}

void AvatarRigCache::SaveToDisk(const std::string& path, const AvatarRig& rig)
{
	try
	{
		Directory::create(CacheDirectory());

		auto file = File::create_always(path);

		file->write(&CacheMagic, sizeof(CacheMagic));
		file->write(&CacheVersion, sizeof(CacheVersion));
		file->write(&rig.MeshContentHash, sizeof(rig.MeshContentHash));

		int32_t frameVerts = rig.FrameVerts;
		int32_t bindFrame = rig.BindFrame;
		uint8_t validByte = rig.Valid ? 1 : 0;
		uint8_t fallbackByte = rig.UsedFallbackTier ? 1 : 0;
		file->write(&frameVerts, sizeof(frameVerts));
		file->write(&bindFrame, sizeof(bindFrame));
		file->write(&validByte, sizeof(validByte));
		file->write(&fallbackByte, sizeof(fallbackByte));

		WriteString(file, rig.Diagnostic);
		WriteString(file, rig.MeshIdentity);

		uint32_t jointCount = (uint32_t)rig.Joints.size();
		file->write(&jointCount, sizeof(jointCount));
		for (const AvatarJoint& joint : rig.Joints)
		{
			uint8_t role = (uint8_t)joint.Role;
			int32_t parent = joint.Parent;
			int32_t vertexCount = joint.VertexCount;
			file->write(&role, sizeof(role));
			file->write(&parent, sizeof(parent));
			WriteVec3(file, joint.BindOrigin);
			file->write(&vertexCount, sizeof(vertexCount));
		}

		uint32_t vertexJointCount = (uint32_t)rig.VertexJoint.size();
		file->write(&vertexJointCount, sizeof(vertexJointCount));
		if (vertexJointCount > 0)
			file->write(rig.VertexJoint.data(), vertexJointCount);

		uint32_t bindVertCount = (uint32_t)rig.BindVertices.size();
		file->write(&bindVertCount, sizeof(bindVertCount));
		for (const vec3& v : rig.BindVertices)
			WriteVec3(file, v);

		uint32_t bindNormalCount = (uint32_t)rig.BindNormals.size();
		file->write(&bindNormalCount, sizeof(bindNormalCount));
		for (const vec3& v : rig.BindNormals)
			WriteVec3(file, v);
	}
	catch (const std::exception& e)
	{
		LogMessage(std::string("AvatarRigCache: failed to save cache file: ") + e.what());
	}
}

bool AvatarRigCache::TryLoadFromDisk(const std::string& path, AvatarRig& outRig)
{
	auto file = File::try_open_existing(path);
	if (!file)
		return false;

	try
	{
		uint32_t magic = file->read_uint32();
		uint32_t version = file->read_uint32();
		if (magic != CacheMagic || version != CacheVersion)
			return false;

		outRig.MeshContentHash = file->read_uint64();
		outRig.FrameVerts = (int)file->read_int32();
		outRig.BindFrame = (int)file->read_int32();
		outRig.Valid = file->read_uint8() != 0;
		outRig.UsedFallbackTier = file->read_uint8() != 0;

		outRig.Diagnostic = ReadString(file);
		outRig.MeshIdentity = ReadString(file);

		uint32_t jointCount = file->read_uint32();
		outRig.Joints.resize(jointCount);
		for (uint32_t i = 0; i < jointCount; i++)
		{
			AvatarJoint& joint = outRig.Joints[i];
			joint.Role = (AvatarJointRole)file->read_uint8();
			joint.Parent = (int)file->read_int32();
			joint.BindOrigin = ReadVec3(file);
			joint.VertexCount = (int)file->read_int32();
		}

		uint32_t vertexJointCount = file->read_uint32();
		outRig.VertexJoint.resize(vertexJointCount);
		if (vertexJointCount > 0)
			file->read(outRig.VertexJoint.data(), vertexJointCount);

		uint32_t bindVertCount = file->read_uint32();
		outRig.BindVertices.resize(bindVertCount);
		for (uint32_t i = 0; i < bindVertCount; i++)
			outRig.BindVertices[i] = ReadVec3(file);

		uint32_t bindNormalCount = file->read_uint32();
		outRig.BindNormals.resize(bindNormalCount);
		for (uint32_t i = 0; i < bindNormalCount; i++)
			outRig.BindNormals[i] = ReadVec3(file);
	}
	catch (const std::exception&)
	{
		return false;
	}

	return true;
}

void AvatarRigCache::ClearInMemoryCache()
{
	InMemoryCache.clear();
}

const AvatarRig& AvatarRigCache::GetOrBuild(UMesh* mesh)
{
	static AvatarRig invalidRig;
	if (!mesh)
	{
		invalidRig.Valid = false;
		invalidRig.Diagnostic = "not riggable: no mesh";
		return invalidRig;
	}

	std::string identity = (mesh->package ? mesh->package->GetPackageName().ToString() + "." : std::string()) + mesh->Name.ToString();

	auto it = InMemoryCache.find(identity);
	if (it != InMemoryCache.end())
		return it->second;

	uint64_t contentHash = AvatarAutoRig::ComputeMeshContentHash(mesh);
	std::string path = CacheFilePath(identity, contentHash);

	AvatarRig rig;
	if (TryLoadFromDisk(path, rig) && rig.MeshContentHash == contentHash)
	{
		auto inserted = InMemoryCache.emplace(identity, std::move(rig));
		return inserted.first->second;
	}

	rig = AvatarAutoRig::Build(mesh);
	if (rig.Valid)
		SaveToDisk(path, rig);

	auto inserted = InMemoryCache.emplace(identity, std::move(rig));
	return inserted.first->second;
}
