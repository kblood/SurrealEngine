#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace BotAI
{
	struct Vector3
	{
		double X = 0.0;
		double Y = 0.0;
		double Z = 0.0;
	};

	enum class Action
	{
		Idle,
		Explore,
		AcquireItem,
		HuntEnemy,
		AttackEnemy,
		Retreat,
		InvestigateSound,
		RecoverFromStuck
	};

	struct EnemyObservation
	{
		std::string Identity;
		Vector3 Position;
		Vector3 Velocity;
		double Distance = 0.0;
		double Confidence = 0.0;
		double SecondsSinceObservation = 0.0;
		double EstimatedHealthFraction = 1.0;
		bool Visible = false;
		bool HasLineOfSight = false;
		bool FiringAtBot = false;
	};

	struct ItemObservation
	{
		std::string Identity;
		Vector3 Position;
		double Distance = 0.0;
		double Utility = 0.0;
		double RouteDanger = 0.0;
		bool Reachable = false;
	};

	struct Observation
	{
		uint64_t Tick = 0;
		double DeltaSeconds = 0.0;
		double HealthFraction = 1.0;
		double ArmorFraction = 0.0;
		double AmmunitionFraction = 0.0;
		double RecentIncomingDamage = 0.0;
		double StuckSeconds = 0.0;
		double ObjectiveUtility = 0.0;
		bool HasUsableWeapon = false;
		std::vector<EnemyObservation> Enemies;
		std::vector<ItemObservation> Items;
	};

	struct Alternative
	{
		Action Candidate = Action::Idle;
		double Score = 0.0;
		std::string Reason;
	};

	struct Decision
	{
		Action Selected = Action::Idle;
		std::string TargetIdentity;
		double Score = 0.0;
		std::string Reason;
		std::vector<Alternative> Alternatives;
	};

	class Policy
	{
	public:
		virtual ~Policy() = default;
		virtual const char* GetId() const = 0;
		virtual uint32_t GetVersion() const = 0;
		virtual void Reset(uint64_t seed) = 0;
		virtual Decision Tick(const Observation& observation) = 0;
	};
}
