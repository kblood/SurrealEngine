#pragma once

#include "BotBenchmarkDeathAttribution.h"

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace BotBenchmarkDeathAttribution
{
	enum class DamageFrameKind
	{
		Override,
		Canonical,
	};

	enum class EnvironmentalSource
	{
		PainTimer,
		FellOutOfWorld,
		TakeFallingDamage,
		MoverEncroachingOn,
		Landed,
	};

	enum class CoordinatorStatus
	{
		Accepted,
		Ignored,
		InvalidConfiguration,
		InvalidContract,
		InvalidTime,
		InvalidParticipant,
		InvalidToken,
		OutOfOrder,
		DuplicateResult,
	};

	struct ScopeToken
	{
		uint64_t Value = 0;

		explicit operator bool() const { return Value != 0; }
	};

	struct BeginScopeResult
	{
		CoordinatorStatus Status = CoordinatorStatus::InvalidConfiguration;
		ScopeToken Token;
		bool IsOutermost = false;
	};

	struct TakeDamageEnter
	{
		std::string Victim;
		DamageInstigator Instigator = DamageInstigator::NoneOrNonPlayer;
		double TimeSeconds = 0.0;
		int PreHealth = 0;
		DamageFrameKind FrameKind = DamageFrameKind::Override;
		bool ContractMatches = true;
	};

	struct EnvironmentalSourceEnter
	{
		std::string Victim;
		EnvironmentalSource Source = EnvironmentalSource::PainTimer;
		double TimeSeconds = 0.0;
		bool ContractMatches = true;
	};

	struct KilledEnter
	{
		std::string Victim;
		DeathKiller Killer = DeathKiller::None;
		double TimeSeconds = 0.0;
		bool ContractMatches = true;
	};

	struct KilledResult
	{
		CoordinatorStatus Status = CoordinatorStatus::InvalidConfiguration;
		std::optional<Decision> Attribution;
		std::optional<EnvironmentalSource> Source;
	};

	class Coordinator
	{
	public:
		explicit Coordinator(double recentContributionWindowSeconds);

		bool IsReady() const;
		BeginScopeResult EnterTakeDamage(TakeDamageEnter observation);
		CoordinatorStatus ObserveAddVelocity(std::string_view victim, bool nonzeroVelocity);
		CoordinatorStatus ObserveTakeDamageResult(ScopeToken token, int postHealth);
		CoordinatorStatus ExitTakeDamage(ScopeToken token);

		BeginScopeResult EnterEnvironmentalSource(EnvironmentalSourceEnter observation);
		CoordinatorStatus ExitEnvironmentalSource(ScopeToken token);

		BeginScopeResult EnterKilled(KilledEnter observation);
		KilledResult ObserveKilledResult(ScopeToken token);
		CoordinatorStatus ExitKilled(ScopeToken token);

		const State* FindLifeState(std::string_view victim) const;
		void ResetLife(std::string_view victim);
		bool HasActiveScopes() const;

	private:
		struct DamageFrame
		{
			ScopeToken Token;
			TakeDamageEnter Observation;
			bool SawNonzeroAddVelocity = false;
			bool HasNormalResult = false;
			int PostHealth = 0;
		};

		struct EnvironmentalFrame
		{
			ScopeToken Token;
			EnvironmentalSourceEnter Observation;
		};

		struct KilledFrame
		{
			ScopeToken Token;
			KilledEnter Observation;
			bool IsOutermost = false;
			bool HasNormalResult = false;
			bool HasEnvironmentalEvidence = false;
			std::optional<EnvironmentalSource> Source;
		};

		ScopeToken NextToken();
		CoordinatorStatus Validate(bool contractMatches, std::string_view victim,
			double timeSeconds) const;
		std::optional<EnvironmentalSource> FindEnvironmentalSource(
			std::string_view victim) const;

		double RecentContributionWindowSeconds = 0.0;
		uint64_t NextTokenValue = 1;
		std::map<std::string, State, std::less<>> LifeStates;
		std::vector<DamageFrame> DamageFrames;
		std::vector<EnvironmentalFrame> EnvironmentalFrames;
		std::vector<KilledFrame> KilledFrames;
	};
}
