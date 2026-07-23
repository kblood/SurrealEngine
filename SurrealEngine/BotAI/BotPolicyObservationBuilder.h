#pragma once

#include "BotPolicy.h"

#include <cstdint>
#include <string>
#include <vector>

namespace BotAI
{
	struct RuntimeSelfSnapshot
	{
		std::string StableIdentity;
		Vector3 Position;
		double HealthFraction = 1.0;
		double ArmorFraction = 0.0;
		double AmmunitionFraction = 0.0;
		double RecentIncomingDamage = 0.0;
		double StuckSeconds = 0.0;
		bool HasUsableWeapon = false;
	};

	struct RuntimeEnemySnapshot
	{
		std::string StableIdentity;
		Vector3 Position;
		Vector3 Velocity;
		double Confidence = 0.0;
		double SecondsSinceObservation = 0.0;
		double EstimatedHealthFraction = 1.0;
		bool Visible = false;
		bool HasLineOfSight = false;
		bool FiringAtBot = false;
		bool Hostile = false;
		bool Dead = false;
		bool Deleted = false;
	};

	struct RuntimeItemSnapshot
	{
		std::string StableIdentity;
		Vector3 Position;
		double Utility = 0.0;
		double RouteDanger = 0.0;
		bool Reachable = false;
		bool Deleted = false;
	};

	struct RuntimeObservationSnapshot
	{
		uint64_t Tick = 0;
		double DeltaSeconds = 0.0;
		double ObjectiveUtility = 0.0;
		RuntimeSelfSnapshot Self;
		std::vector<RuntimeEnemySnapshot> Enemies;
		std::vector<RuntimeItemSnapshot> Items;
	};

	class PolicyObservationBuilder
	{
	public:
		static Observation Build(const RuntimeObservationSnapshot& snapshot);
	};
}
