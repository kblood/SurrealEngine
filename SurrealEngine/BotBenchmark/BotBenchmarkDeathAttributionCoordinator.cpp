#include "BotBenchmarkDeathAttributionCoordinator.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace BotBenchmarkDeathAttribution
{
	Coordinator::Coordinator(double recentContributionWindowSeconds)
		: RecentContributionWindowSeconds(recentContributionWindowSeconds)
	{
	}

	bool Coordinator::IsReady() const
	{
		return std::isfinite(RecentContributionWindowSeconds) &&
			RecentContributionWindowSeconds >= 0.0;
	}

	ScopeToken Coordinator::NextToken()
	{
		return { NextTokenValue++ };
	}

	CoordinatorStatus Coordinator::Validate(bool contractMatches, std::string_view victim,
		double timeSeconds) const
	{
		if (!IsReady())
			return CoordinatorStatus::InvalidConfiguration;
		if (!contractMatches)
			return CoordinatorStatus::InvalidContract;
		if (victim.empty())
			return CoordinatorStatus::InvalidParticipant;
		if (!std::isfinite(timeSeconds) || timeSeconds < 0.0)
			return CoordinatorStatus::InvalidTime;
		return CoordinatorStatus::Accepted;
	}

	BeginScopeResult Coordinator::EnterTakeDamage(TakeDamageEnter observation)
	{
		const CoordinatorStatus status = Validate(observation.ContractMatches,
			observation.Victim, observation.TimeSeconds);
		if (status != CoordinatorStatus::Accepted)
			return { status };

		const ScopeToken token = NextToken();
		DamageFrames.push_back({ token, std::move(observation) });
		return { CoordinatorStatus::Accepted, token, DamageFrames.size() == 1 };
	}

	CoordinatorStatus Coordinator::ObserveAddVelocity(std::string_view victim,
		bool nonzeroVelocity)
	{
		if (!IsReady())
			return CoordinatorStatus::InvalidConfiguration;
		if (victim.empty())
			return CoordinatorStatus::InvalidParticipant;

		for (auto frame = DamageFrames.rbegin(); frame != DamageFrames.rend(); ++frame)
		{
			if (frame->Observation.FrameKind == DamageFrameKind::Canonical &&
				frame->Observation.Victim == victim)
			{
				frame->SawNonzeroAddVelocity |= nonzeroVelocity;
				return CoordinatorStatus::Accepted;
			}
		}
		return CoordinatorStatus::Ignored;
	}

	CoordinatorStatus Coordinator::ObserveTakeDamageResult(ScopeToken token, int postHealth)
	{
		if (!token)
			return CoordinatorStatus::InvalidToken;
		if (DamageFrames.empty() || DamageFrames.back().Token.Value != token.Value)
			return CoordinatorStatus::OutOfOrder;

		DamageFrame& frame = DamageFrames.back();
		if (frame.HasNormalResult)
			return CoordinatorStatus::DuplicateResult;
		frame.HasNormalResult = true;
		frame.PostHealth = postHealth;
		return CoordinatorStatus::Accepted;
	}

	CoordinatorStatus Coordinator::ExitTakeDamage(ScopeToken token)
	{
		if (!token)
			return CoordinatorStatus::InvalidToken;
		if (DamageFrames.empty() || DamageFrames.back().Token.Value != token.Value)
			return CoordinatorStatus::OutOfOrder;

		DamageFrame frame = std::move(DamageFrames.back());
		DamageFrames.pop_back();
		if (frame.Observation.FrameKind != DamageFrameKind::Canonical ||
			!frame.HasNormalResult || frame.Observation.PreHealth <= 0)
			return CoordinatorStatus::Accepted;

		const bool lostHealth = frame.PostHealth < frame.Observation.PreHealth;
		if (!lostHealth && !frame.SawNonzeroAddVelocity)
			return CoordinatorStatus::Accepted;
		if (frame.Observation.Instigator != DamageInstigator::EnemyPlayer)
			return CoordinatorStatus::Accepted;

		State& state = LifeStates[frame.Observation.Victim];
		if ((state.HasEnemyContribution &&
			state.LastEnemyContributionSeconds > frame.Observation.TimeSeconds) ||
			(state.HasEnemyMomentumContribution &&
			state.LastEnemyMomentumContributionSeconds > frame.Observation.TimeSeconds))
			return CoordinatorStatus::InvalidTime;
		state = ObserveDamage(state, {
			frame.Observation.TimeSeconds,
			frame.Observation.Instigator,
			frame.SawNonzeroAddVelocity,
		});
		return CoordinatorStatus::Accepted;
	}

	BeginScopeResult Coordinator::EnterEnvironmentalSource(EnvironmentalSourceEnter observation)
	{
		const CoordinatorStatus status = Validate(observation.ContractMatches,
			observation.Victim, observation.TimeSeconds);
		if (status != CoordinatorStatus::Accepted)
			return { status };

		const ScopeToken token = NextToken();
		const bool outermost = std::none_of(EnvironmentalFrames.begin(), EnvironmentalFrames.end(),
			[&observation](const EnvironmentalFrame& frame)
			{
				return frame.Observation.Victim == observation.Victim;
			});
		EnvironmentalFrames.push_back({ token, std::move(observation) });
		return { CoordinatorStatus::Accepted, token, outermost };
	}

	CoordinatorStatus Coordinator::ExitEnvironmentalSource(ScopeToken token)
	{
		if (!token)
			return CoordinatorStatus::InvalidToken;
		if (EnvironmentalFrames.empty() || EnvironmentalFrames.back().Token.Value != token.Value)
			return CoordinatorStatus::OutOfOrder;
		EnvironmentalFrames.pop_back();
		return CoordinatorStatus::Accepted;
	}

	std::optional<EnvironmentalSource> Coordinator::FindEnvironmentalSource(
		std::string_view victim) const
	{
		for (auto frame = EnvironmentalFrames.rbegin(); frame != EnvironmentalFrames.rend(); ++frame)
		{
			if (frame->Observation.Victim == victim)
				return frame->Observation.Source;
		}
		return {};
	}

	BeginScopeResult Coordinator::EnterKilled(KilledEnter observation)
	{
		const CoordinatorStatus status = Validate(observation.ContractMatches,
			observation.Victim, observation.TimeSeconds);
		if (status != CoordinatorStatus::Accepted)
			return { status };

		const ScopeToken token = NextToken();
		const bool outermost = std::none_of(KilledFrames.begin(), KilledFrames.end(),
			[&observation](const KilledFrame& frame)
			{
				return frame.Observation.Victim == observation.Victim;
			});
		KilledFrame frame;
		frame.Token = token;
		frame.Observation = std::move(observation);
		frame.IsOutermost = outermost;
		if (outermost)
		{
			frame.Source = FindEnvironmentalSource(frame.Observation.Victim);
			frame.HasEnvironmentalEvidence = frame.Source.has_value();
		}
		KilledFrames.push_back(std::move(frame));
		return { CoordinatorStatus::Accepted, token, outermost };
	}

	KilledResult Coordinator::ObserveKilledResult(ScopeToken token)
	{
		if (!token)
			return { CoordinatorStatus::InvalidToken };
		if (KilledFrames.empty() || KilledFrames.back().Token.Value != token.Value)
			return { CoordinatorStatus::OutOfOrder };

		KilledFrame& frame = KilledFrames.back();
		if (frame.HasNormalResult)
			return { CoordinatorStatus::DuplicateResult };
		frame.HasNormalResult = true;
		if (!frame.IsOutermost)
			return { CoordinatorStatus::Accepted };

		State state;
		auto existing = LifeStates.find(frame.Observation.Victim);
		if (existing != LifeStates.end())
			state = existing->second;
		if ((state.HasEnemyContribution &&
			state.LastEnemyContributionSeconds > frame.Observation.TimeSeconds) ||
			(state.HasEnemyMomentumContribution &&
			state.LastEnemyMomentumContributionSeconds > frame.Observation.TimeSeconds))
			return { CoordinatorStatus::InvalidTime };
		Decision decision = ClassifyDeath(state, {
			frame.Observation.TimeSeconds,
			frame.Observation.Killer,
			frame.HasEnvironmentalEvidence,
		}, RecentContributionWindowSeconds);

		if (decision.NextState.HasEnemyContribution ||
			decision.NextState.HasEnemyMomentumContribution)
			LifeStates[frame.Observation.Victim] = decision.NextState;
		else
			LifeStates.erase(frame.Observation.Victim);

		return { CoordinatorStatus::Accepted, std::move(decision), frame.Source };
	}

	CoordinatorStatus Coordinator::ExitKilled(ScopeToken token)
	{
		if (!token)
			return CoordinatorStatus::InvalidToken;
		if (KilledFrames.empty() || KilledFrames.back().Token.Value != token.Value)
			return CoordinatorStatus::OutOfOrder;
		KilledFrames.pop_back();
		return CoordinatorStatus::Accepted;
	}

	const State* Coordinator::FindLifeState(std::string_view victim) const
	{
		auto state = LifeStates.find(victim);
		return state != LifeStates.end() ? &state->second : nullptr;
	}

	void Coordinator::ResetLife(std::string_view victim)
	{
		auto state = LifeStates.find(victim);
		if (state != LifeStates.end())
			LifeStates.erase(state);
	}

	bool Coordinator::HasActiveScopes() const
	{
		return !DamageFrames.empty() || !EnvironmentalFrames.empty() || !KilledFrames.empty();
	}
}
