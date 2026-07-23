#include "TacticalBotPolicy.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <sstream>
#include <utility>

namespace BotAI
{
	namespace
	{
		constexpr double EnemyMemorySeconds = 4.0;
		constexpr double UncertainRefreshSeconds = 2.5;
		constexpr double MaximumUncertainAgeSeconds = 3.0;
		constexpr double MinimumUncertainConfidence = 0.35;
		constexpr double StuckThresholdSeconds = 1.0;
		constexpr double RecoveryClearThresholdSeconds = 0.25;

		double FiniteOr(double value, double fallback)
		{
			return std::isfinite(value) ? value : fallback;
		}

		double Fraction(double value)
		{
			return std::clamp(FiniteOr(value, 0.0), 0.0, 1.0);
		}

		double ThreatScore(const EnemyObservation& enemy)
		{
			const double distance = std::max(0.0, FiniteOr(enemy.Distance, std::numeric_limits<double>::max()));
			return Fraction(enemy.Confidence) * 100.0 +
				(enemy.FiringAtBot ? 25.0 : 0.0) +
				(1.0 - Fraction(enemy.EstimatedHealthFraction)) * 5.0 +
				10.0 / (1.0 + distance);
		}

		double UncertainScore(const EnemyObservation& enemy)
		{
			const double age = std::max(0.0, FiniteOr(enemy.SecondsSinceObservation, MaximumUncertainAgeSeconds));
			return Fraction(enemy.Confidence) * 100.0 - age * 5.0 + (enemy.FiringAtBot ? 10.0 : 0.0);
		}

		const EnemyObservation* SelectEnemy(const Observation& observation, bool visible)
		{
			const EnemyObservation* selected = nullptr;
			double selectedScore = -std::numeric_limits<double>::infinity();
			for (const EnemyObservation& enemy : observation.Enemies)
			{
				if (enemy.Identity.empty())
					continue;

				const bool isVisible = enemy.Visible && enemy.HasLineOfSight && Fraction(enemy.Confidence) > 0.0;
				if (visible != isVisible)
					continue;

				if (!visible)
				{
					const double age = FiniteOr(enemy.SecondsSinceObservation, MaximumUncertainAgeSeconds + 1.0);
					if (Fraction(enemy.Confidence) < MinimumUncertainConfidence || age < 0.0 || age > MaximumUncertainAgeSeconds)
						continue;
				}

				const double score = visible ? ThreatScore(enemy) : UncertainScore(enemy);
				if (!selected || score > selectedScore || (score == selectedScore && enemy.Identity < selected->Identity))
				{
					selected = &enemy;
					selectedScore = score;
				}
			}
			return selected;
		}

		const ItemObservation* SelectItem(const Observation& observation, double& selectedScore)
		{
			const ItemObservation* selected = nullptr;
			selectedScore = -std::numeric_limits<double>::infinity();
			for (const ItemObservation& item : observation.Items)
			{
				if (!item.Reachable || item.Identity.empty())
					continue;

				const double score = FiniteOr(item.Utility, 0.0) - std::max(0.0, FiniteOr(item.RouteDanger, 0.0)) -
					std::max(0.0, FiniteOr(item.Distance, 0.0)) * 0.001;
				if (!selected || score > selectedScore || (score == selectedScore && item.Identity < selected->Identity))
				{
					selected = &item;
					selectedScore = score;
				}
			}
			return selected;
		}

		Decision MakeDecision(Action action, std::string target, double score, std::string reason)
		{
			Decision decision;
			decision.Selected = action;
			decision.TargetIdentity = std::move(target);
			decision.Score = score;
			decision.Reason = std::move(reason);
			return decision;
		}
	}

	const char* TacticalBotPolicy::GetId() const
	{
		return "tactical-state";
	}

	uint32_t TacticalBotPolicy::GetVersion() const
	{
		return 1;
	}

	void TacticalBotPolicy::Reset(uint64_t seed)
	{
		(void)seed;
		State = TacticalState::IdleExplore;
		EnemyMemory = {};
		ElapsedSeconds = 0.0;
		RecoveryClearSeconds = 0.0;
	}

	Decision TacticalBotPolicy::Tick(const Observation& observation)
	{
		const double deltaSeconds = std::max(0.0, FiniteOr(observation.DeltaSeconds, 0.0));
		if (deltaSeconds > std::numeric_limits<double>::max() - ElapsedSeconds)
			ElapsedSeconds = std::numeric_limits<double>::max();
		else
			ElapsedSeconds += deltaSeconds;

		if (EnemyMemory.Valid && ElapsedSeconds >= EnemyMemory.ExpiresAtSeconds)
			EnemyMemory = {};

		const EnemyObservation* visibleEnemy = SelectEnemy(observation, true);
		const EnemyObservation* uncertainEnemy = SelectEnemy(observation, false);
		if (visibleEnemy)
		{
			EnemyMemory.Identity = visibleEnemy->Identity;
			EnemyMemory.Position = visibleEnemy->Position;
			EnemyMemory.Confidence = Fraction(visibleEnemy->Confidence);
			EnemyMemory.ExpiresAtSeconds = ElapsedSeconds + EnemyMemorySeconds;
			EnemyMemory.Valid = true;
		}
		else if (EnemyMemory.Valid)
		{
			for (const EnemyObservation& enemy : observation.Enemies)
			{
				if (enemy.Identity == EnemyMemory.Identity && !enemy.Visible &&
					Fraction(enemy.Confidence) >= MinimumUncertainConfidence)
				{
					EnemyMemory.Position = enemy.Position;
					EnemyMemory.Confidence = Fraction(enemy.Confidence);
					EnemyMemory.ExpiresAtSeconds = std::max(
						EnemyMemory.ExpiresAtSeconds, ElapsedSeconds + UncertainRefreshSeconds);
					break;
				}
			}
		}

		const double stuckSeconds = std::max(0.0, FiniteOr(observation.StuckSeconds, 0.0));
		if (stuckSeconds >= StuckThresholdSeconds)
		{
			State = TacticalState::UnstuckRecovery;
			RecoveryClearSeconds = 0.0;
			std::ostringstream reason;
			reason << "unstuck-recovery: stuck for " << stuckSeconds << " seconds";
			return MakeDecision(Action::RecoverFromStuck, {}, 1000.0 + stuckSeconds, reason.str());
		}
		if (State == TacticalState::UnstuckRecovery)
		{
			RecoveryClearSeconds += deltaSeconds;
			if (RecoveryClearSeconds < RecoveryClearThresholdSeconds)
			{
				std::ostringstream reason;
				reason << "unstuck-recovery: movement clear for " << RecoveryClearSeconds << " seconds";
				return MakeDecision(Action::RecoverFromStuck, {}, 900.0, reason.str());
			}
			RecoveryClearSeconds = 0.0;
		}

		const double health = Fraction(observation.HealthFraction);
		const double ammunition = Fraction(observation.AmmunitionFraction);
		const bool criticalResources = !observation.HasUsableWeapon || health <= 0.20 || ammunition <= 0.02;
		const bool lowResources = health <= 0.35 || ammunition <= 0.15;
		if (criticalResources || (!visibleEnemy && lowResources))
		{
			State = TacticalState::ResupplyRetreat;
			double itemScore = 0.0;
			const ItemObservation* item = SelectItem(observation, itemScore);
			if (item && itemScore > 0.0)
			{
				return MakeDecision(Action::AcquireItem, item->Identity, 700.0 + itemScore,
					"resupply-retreat: acquiring reachable item '" + item->Identity + "'");
			}
			return MakeDecision(Action::Retreat, {}, 700.0,
				"resupply-retreat: low resources without a worthwhile reachable item");
		}

		if (visibleEnemy)
		{
			State = TacticalState::AcquireAttack;
			return MakeDecision(Action::AttackEnemy, visibleEnemy->Identity, 600.0 + ThreatScore(*visibleEnemy),
				"acquire-attack: visible line-of-sight threat '" + visibleEnemy->Identity + "'");
		}

		if (EnemyMemory.Valid)
		{
			State = TacticalState::HuntRememberedEnemy;
			const double remaining = std::max(0.0, EnemyMemory.ExpiresAtSeconds - ElapsedSeconds);
			std::ostringstream reason;
			reason << "hunt-remembered-enemy: pursuing '" << EnemyMemory.Identity << "' for " << remaining << " more seconds";
			return MakeDecision(Action::HuntEnemy, EnemyMemory.Identity, 500.0 + EnemyMemory.Confidence * 100.0, reason.str());
		}

		if (uncertainEnemy)
		{
			State = TacticalState::InvestigateUncertainObservation;
			std::ostringstream reason;
			reason << "investigate-uncertain-observation: '" << uncertainEnemy->Identity << "' confidence "
				<< Fraction(uncertainEnemy->Confidence);
			return MakeDecision(Action::InvestigateSound, uncertainEnemy->Identity,
				400.0 + UncertainScore(*uncertainEnemy), reason.str());
		}

		State = TacticalState::IdleExplore;
		const double objectiveUtility = std::max(0.0, FiniteOr(observation.ObjectiveUtility, 0.0));
		if (objectiveUtility > 0.0)
			return MakeDecision(Action::Explore, {}, 100.0 + objectiveUtility, "idle-explore: pursuing observed objective utility");
		return MakeDecision(Action::Idle, {}, 0.0, "idle-explore: no current subjective stimulus");
	}
}
