#include "UtilityBotPolicy.h"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <utility>
#include <vector>

namespace BotAI
{
	namespace
	{
		constexpr double StuckRecoveryThreshold = 1.0;
		constexpr double HysteresisBand = 8.0;
		constexpr double UnavailableScore = -1.0;

		struct Candidate
		{
			Action CandidateAction = Action::Idle;
			std::string TargetIdentity;
			double Score = UnavailableScore;
			std::string Reason;
			bool Available = false;
		};

		double FiniteOr(double value, double fallback = 0.0)
		{
			return std::isfinite(value) ? value : fallback;
		}

		double Unit(double value)
		{
			return std::clamp(FiniteOr(value), 0.0, 1.0);
		}

		double NonNegative(double value)
		{
			return std::max(FiniteOr(value), 0.0);
		}

		int ActionPriority(Action action)
		{
			switch (action)
			{
			case Action::RecoverFromStuck: return 0;
			case Action::Retreat: return 1;
			case Action::AttackEnemy: return 2;
			case Action::HuntEnemy: return 3;
			case Action::AcquireItem: return 4;
			case Action::Explore: return 5;
			case Action::InvestigateSound: return 6;
			case Action::Idle: return 7;
			}
			return 8;
		}

		bool BetterCandidate(const Candidate& left, const Candidate& right)
		{
			if (left.Available != right.Available)
				return left.Available;
			if (left.Score != right.Score)
				return left.Score > right.Score;
			if (left.TargetIdentity != right.TargetIdentity)
				return left.TargetIdentity < right.TargetIdentity;
			return ActionPriority(left.CandidateAction) < ActionPriority(right.CandidateAction);
		}

		std::string Describe(const char* label, double score, const std::string& target = {})
		{
			std::ostringstream text;
			text << label << " score=" << score;
			if (!target.empty())
				text << " target=" << target;
			return text.str();
		}

		Candidate SelectTargetCandidate(std::vector<Candidate> candidates, Action action, const char* unavailableReason)
		{
			if (candidates.empty())
				return { action, {}, UnavailableScore, unavailableReason, false };
			return *std::min_element(candidates.begin(), candidates.end(), [](const Candidate& left, const Candidate& right)
			{
				return BetterCandidate(left, right);
			});
		}

		const EnemyObservation* FindEnemy(const Observation& observation, const std::string& identity)
		{
			auto it = std::find_if(observation.Enemies.begin(), observation.Enemies.end(), [&](const EnemyObservation& enemy)
			{
				return enemy.Identity == identity;
			});
			return it == observation.Enemies.end() ? nullptr : &*it;
		}

		const ItemObservation* FindItem(const Observation& observation, const std::string& identity)
		{
			auto it = std::find_if(observation.Items.begin(), observation.Items.end(), [&](const ItemObservation& item)
			{
				return item.Identity == identity;
			});
			return it == observation.Items.end() ? nullptr : &*it;
		}

		double AttackScore(const Observation& observation, const EnemyObservation& enemy)
		{
			const double health = Unit(observation.HealthFraction);
			const double distancePenalty = std::min(NonNegative(enemy.Distance) / 250.0, 20.0);
			return 70.0 + Unit(enemy.Confidence) * 20.0 + (enemy.FiringAtBot ? 12.0 : 0.0)
				+ Unit(enemy.EstimatedHealthFraction) * -8.0 + health * 8.0 - distancePenalty;
		}

		double HuntScore(const EnemyObservation& enemy)
		{
			return 42.0 + Unit(enemy.Confidence) * 30.0
				- std::min(NonNegative(enemy.SecondsSinceObservation) * 3.0, 36.0)
				- std::min(NonNegative(enemy.Distance) / 400.0, 12.0);
		}

		double ItemScore(const ItemObservation& item)
		{
			return 25.0 + FiniteOr(item.Utility) * 70.0
				- std::min(NonNegative(item.Distance) / 100.0, 20.0)
				- NonNegative(item.RouteDanger) * 30.0;
		}

		Candidate ActiveCandidate(const Observation& observation, Action action, const std::string& identity)
		{
			if (action == Action::AttackEnemy)
			{
				const EnemyObservation* enemy = FindEnemy(observation, identity);
				if (enemy && observation.HasUsableWeapon && enemy->Visible && enemy->HasLineOfSight)
				{
					const double score = AttackScore(observation, *enemy);
					return { action, identity, score, Describe("visible combat", score, identity), true };
				}
			}
			else if (action == Action::HuntEnemy)
			{
				const EnemyObservation* enemy = FindEnemy(observation, identity);
				if (enemy && !(observation.HasUsableWeapon && enemy->Visible && enemy->HasLineOfSight)
					&& Unit(enemy->Confidence) > 0.15 && NonNegative(enemy->SecondsSinceObservation) <= 15.0)
				{
					const double score = HuntScore(*enemy);
					return { action, identity, score, Describe("remembered enemy", score, identity), true };
				}
			}
			else if (action == Action::AcquireItem)
			{
				const ItemObservation* item = FindItem(observation, identity);
				if (item && item->Reachable)
				{
					const double score = ItemScore(*item);
					return { action, identity, score, Describe("reachable item", score, identity), true };
				}
			}
			return {};
		}
	}

	const char* UtilityBotPolicy::GetId() const
	{
		return "utility-arena";
	}

	uint32_t UtilityBotPolicy::GetVersion() const
	{
		return 1;
	}

	void UtilityBotPolicy::Reset(uint64_t)
	{
		ActionStack.clear();
	}

	Decision UtilityBotPolicy::Tick(const Observation& observation)
	{
		std::vector<Candidate> candidates;

		const double stuckSeconds = NonNegative(observation.StuckSeconds);
		const bool stuck = stuckSeconds >= StuckRecoveryThreshold;
		candidates.push_back(stuck
			? Candidate{ Action::RecoverFromStuck, {}, 1000.0 + stuckSeconds, Describe("stuck recovery preemption", 1000.0 + stuckSeconds), true }
			: Candidate{ Action::RecoverFromStuck, {}, UnavailableScore, "not stuck", false });

		std::vector<Candidate> attackTargets;
		for (const EnemyObservation& enemy : observation.Enemies)
		{
			if (!observation.HasUsableWeapon || !enemy.Visible || !enemy.HasLineOfSight)
				continue;
			const double score = AttackScore(observation, enemy);
			attackTargets.push_back({ Action::AttackEnemy, enemy.Identity, score, Describe("visible combat", score, enemy.Identity), true });
		}
		Candidate attack = SelectTargetCandidate(std::move(attackTargets), Action::AttackEnemy, "no visible line-of-sight enemy with a usable weapon");

		const double health = Unit(observation.HealthFraction);
		const double incomingDamage = NonNegative(observation.RecentIncomingDamage);
		std::vector<Candidate> retreatThreats;
		for (const EnemyObservation& enemy : observation.Enemies)
		{
			if (!enemy.Visible && !enemy.FiringAtBot)
				continue;
			const double threat = Unit(enemy.Confidence) * 10.0 + (enemy.FiringAtBot ? 15.0 : 0.0)
				+ std::max(0.0, 10.0 - NonNegative(enemy.Distance) / 150.0);
			retreatThreats.push_back({ Action::Retreat, enemy.Identity, threat, {}, true });
		}
		Candidate retreatThreat = SelectTargetCandidate(std::move(retreatThreats), Action::Retreat, "no immediate threat");
		const EnemyObservation* primaryThreat = retreatThreat.Available ? FindEnemy(observation, retreatThreat.TargetIdentity) : nullptr;
		const bool retreatNeeded = health < 0.45 || incomingDamage > 0.15 || (primaryThreat && primaryThreat->FiringAtBot);
		Candidate retreat{ Action::Retreat, retreatThreat.TargetIdentity, UnavailableScore, "health and incoming threat do not justify retreat", false };
		if (retreatNeeded)
		{
			retreat.Available = true;
			retreat.Score = 35.0 + (1.0 - health) * 85.0 + incomingDamage * 35.0
				+ (retreatThreat.Available ? retreatThreat.Score : 0.0);
			retreat.Reason = Describe("survival pressure", retreat.Score, retreat.TargetIdentity);
		}

		std::vector<Candidate> huntTargets;
		for (const EnemyObservation& enemy : observation.Enemies)
		{
			if (observation.HasUsableWeapon && enemy.Visible && enemy.HasLineOfSight)
				continue;
			if (Unit(enemy.Confidence) <= 0.15 || NonNegative(enemy.SecondsSinceObservation) > 15.0)
				continue;
			const double score = HuntScore(enemy);
			huntTargets.push_back({ Action::HuntEnemy, enemy.Identity, score, Describe("remembered enemy", score, enemy.Identity), true });
		}
		Candidate hunt = SelectTargetCandidate(std::move(huntTargets), Action::HuntEnemy, "no recent enemy memory to pursue");

		std::vector<Candidate> itemTargets;
		for (const ItemObservation& item : observation.Items)
		{
			if (!item.Reachable)
				continue;
			const double score = ItemScore(item);
			itemTargets.push_back({ Action::AcquireItem, item.Identity, score, Describe("reachable item", score, item.Identity), true });
		}
		Candidate acquireItem = SelectTargetCandidate(std::move(itemTargets), Action::AcquireItem, "no reachable item");

		const double exploreScore = 20.0 + FiniteOr(observation.ObjectiveUtility) * 30.0;
		Candidate explore{ Action::Explore, {}, exploreScore, Describe("exploration and objective progress", exploreScore), true };

		candidates.push_back(retreat);
		candidates.push_back(attack);
		candidates.push_back(hunt);
		candidates.push_back(acquireItem);
		candidates.push_back(explore);

		Candidate selected = *std::min_element(candidates.begin(), candidates.end(), [](const Candidate& left, const Candidate& right)
		{
			return BetterCandidate(left, right);
		});

		if (!stuck && !ActionStack.empty() && ActionStack.back().Selected == Action::RecoverFromStuck)
			ActionStack.pop_back();

		if (!stuck && !ActionStack.empty())
		{
			const Action activeAction = ActionStack.back().Selected;
			const std::string& activeTargetIdentity = ActionStack.back().TargetIdentity;
			Candidate active;
			if (activeAction == Action::Explore)
				active = explore;
			else if (activeAction == Action::Retreat)
				active = retreat;
			else
				active = ActiveCandidate(observation, activeAction, activeTargetIdentity);

			if (active.Available && selected.Score - active.Score <= HysteresisBand)
			{
				selected = active;
				selected.Reason += "; persisted within hysteresis band";
			}
		}

		Decision decision;
		decision.Selected = selected.CandidateAction;
		decision.TargetIdentity = selected.TargetIdentity;
		decision.Score = selected.Score;
		decision.Reason = selected.Reason;
		for (const Candidate& candidate : candidates)
			decision.Alternatives.push_back({ candidate.CandidateAction, candidate.Score, candidate.Reason });

		if (stuck)
		{
			if (ActionStack.empty() || ActionStack.back().Selected != Action::RecoverFromStuck)
				ActionStack.push_back({ Action::RecoverFromStuck, {} });
		}
		else if (ActionStack.empty())
		{
			ActionStack.push_back({ selected.CandidateAction, selected.TargetIdentity });
		}
		else
		{
			ActionStack.back() = { selected.CandidateAction, selected.TargetIdentity };
		}
		return decision;
	}
}
