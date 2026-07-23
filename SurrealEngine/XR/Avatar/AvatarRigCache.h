#pragma once

// On-disk cache for AvatarAutoRig output, keyed by mesh identity + content
// hash so extraction runs once per distinct mesh rather than every time the
// avatar renderer sees that Pawn. See Docs/FULLBODY_VR_AVATAR_PLAN.md,
// "Runtime auto-rig" step 7.

#include "AvatarAutoRig.h"
#include <string>
#include <unordered_map>

class UMesh;

class AvatarRigCache
{
public:
	// Returns the rig for `mesh`, using the in-process cache, then the
	// on-disk cache, and finally AvatarAutoRig::Build as a last resort -
	// persisting whichever of the latter two ran. Never throws.
	static const AvatarRig& GetOrBuild(UMesh* mesh);

	// Directory holding cached rig files (created on first use).
	static std::string CacheDirectory();

	// Test/diagnostic hook: drops the in-process cache (not the on-disk one).
	static void ClearInMemoryCache();

private:
	static std::string CacheFilePath(const std::string& meshIdentity, uint64_t contentHash);
	static bool TryLoadFromDisk(const std::string& path, AvatarRig& outRig);
	static void SaveToDisk(const std::string& path, const AvatarRig& rig);

	static std::unordered_map<std::string, AvatarRig> InMemoryCache;
};
