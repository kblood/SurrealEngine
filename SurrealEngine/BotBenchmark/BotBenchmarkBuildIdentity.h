#pragma once

#include <cstdint>
#include <optional>
#include <string>

// Immutable provenance for the executable that creates a benchmark artifact.
// It intentionally has no connection to bot policy or runtime simulation.
struct BotBenchmarkBuildIdentity
{
	std::string Commit;
	std::string Tree;
	bool Dirty = false;
	std::string ExecutableName;
	uint64_t ExecutableSizeBytes = 0;
	std::string ExecutableSha256;
	std::string IdentityId;

	static std::optional<BotBenchmarkBuildIdentity> TryCurrent(std::string& error);
	static std::optional<BotBenchmarkBuildIdentity> TryCreate(std::string commit, std::string tree,
		bool dirty, const std::string& executablePath, std::string& error);
	std::string ToJson() const;
};
