#pragma once

#include <cstdint>

namespace BotAI
{
	constexpr double FallingHazardRecoveryMinimumAnchorDistance = 8.0;
	constexpr double FallingHazardRecoveryMaximumAnchorDistance = 256.0;

	struct FallingHazardRecoveryEntry
	{
		uint64_t LifeId = 0;
		uint64_t FallEpisodeId = 0;
		bool PrefixPromoted = false;
		bool AnchorKnown = false;
		double AnchorX = 0.0;
		double AnchorY = 0.0;
		double CurrentX = 0.0;
		double CurrentY = 0.0;
	};

	enum class FallingHazardRecoveryEligibility
	{
		Ineligible,
		Authorized,
		Debounced
	};

	bool IsValidFallingHazardRecoveryEntry(
		const FallingHazardRecoveryEntry& entry);

	class FallingHazardRecoveryGate
	{
	public:
		void Reset();
		FallingHazardRecoveryEligibility Evaluate(
			const FallingHazardRecoveryEntry& entry);

	private:
		bool HasAuthorizedEntry = false;
		uint64_t AuthorizedLifeId = 0;
		uint64_t AuthorizedFallEpisodeId = 0;
	};
}
