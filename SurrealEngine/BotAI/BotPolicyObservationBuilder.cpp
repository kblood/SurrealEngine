#include "BotPolicyObservationBuilder.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <tuple>
#include <utility>

namespace BotAI
{
	namespace
	{
		bool IsFinite(const Vector3& value)
		{
			return std::isfinite(value.X) && std::isfinite(value.Y) && std::isfinite(value.Z);
		}

		double FiniteOr(double value, double fallback)
		{
			return std::isfinite(value) ? value : fallback;
		}

		double UnitFraction(double value, double fallback)
		{
			return std::clamp(FiniteOr(value, fallback), 0.0, 1.0);
		}

		double NonNegative(double value, double fallback = 0.0)
		{
			return std::max(FiniteOr(value, fallback), 0.0);
		}

		double FiniteDistance(const Vector3& from, const Vector3& to)
		{
			const long double deltaX = static_cast<long double>(to.X) - static_cast<long double>(from.X);
			const long double deltaY = static_cast<long double>(to.Y) - static_cast<long double>(from.Y);
			const long double deltaZ = static_cast<long double>(to.Z) - static_cast<long double>(from.Z);
			const long double distance = std::hypot(deltaX, deltaY, deltaZ);
			const long double maximum = std::numeric_limits<double>::max();
			return distance > maximum ? std::numeric_limits<double>::max() : static_cast<double>(distance);
		}

		Vector3 SafeVelocity(const Vector3& velocity)
		{
			return IsFinite(velocity) ? velocity : Vector3{};
		}

		bool EnemyOrder(const EnemyObservation& left, const EnemyObservation& right)
		{
			return std::tie(left.Identity, left.Position.X, left.Position.Y, left.Position.Z,
				left.Velocity.X, left.Velocity.Y, left.Velocity.Z, left.Distance,
				left.Confidence, left.SecondsSinceObservation, left.EstimatedHealthFraction,
				left.Visible, left.HasLineOfSight, left.FiringAtBot)
				< std::tie(right.Identity, right.Position.X, right.Position.Y, right.Position.Z,
					right.Velocity.X, right.Velocity.Y, right.Velocity.Z, right.Distance,
					right.Confidence, right.SecondsSinceObservation, right.EstimatedHealthFraction,
					right.Visible, right.HasLineOfSight, right.FiringAtBot);
		}

		bool ItemOrder(const ItemObservation& left, const ItemObservation& right)
		{
			return std::tie(left.Identity, left.Position.X, left.Position.Y, left.Position.Z,
				left.Distance, left.Utility, left.RouteDanger, left.Reachable)
				< std::tie(right.Identity, right.Position.X, right.Position.Y, right.Position.Z,
					right.Distance, right.Utility, right.RouteDanger, right.Reachable);
		}
	}

	Observation PolicyObservationBuilder::Build(const RuntimeObservationSnapshot& snapshot)
	{
		Observation observation;
		observation.Tick = snapshot.Tick;
		observation.DeltaSeconds = NonNegative(snapshot.DeltaSeconds);
		observation.ObjectiveUtility = FiniteOr(snapshot.ObjectiveUtility, 0.0);
		observation.HealthFraction = UnitFraction(snapshot.Self.HealthFraction, 0.0);
		observation.ArmorFraction = UnitFraction(snapshot.Self.ArmorFraction, 0.0);
		observation.AmmunitionFraction = UnitFraction(snapshot.Self.AmmunitionFraction, 0.0);
		observation.RecentIncomingDamage = NonNegative(snapshot.Self.RecentIncomingDamage);
		observation.StuckSeconds = NonNegative(snapshot.Self.StuckSeconds);
		observation.HasUsableWeapon = snapshot.Self.HasUsableWeapon;

		if (snapshot.Self.StableIdentity.empty() || !IsFinite(snapshot.Self.Position))
			return observation;

		observation.Enemies.reserve(snapshot.Enemies.size());
		for (const RuntimeEnemySnapshot& enemy : snapshot.Enemies)
		{
			if (enemy.StableIdentity.empty() || enemy.StableIdentity == snapshot.Self.StableIdentity
				|| enemy.Deleted || enemy.Dead || !enemy.Hostile || !IsFinite(enemy.Position))
				continue;

			EnemyObservation sanitized;
			sanitized.Identity = enemy.StableIdentity;
			sanitized.Position = enemy.Position;
			sanitized.Velocity = SafeVelocity(enemy.Velocity);
			sanitized.Distance = FiniteDistance(snapshot.Self.Position, enemy.Position);
			sanitized.Confidence = UnitFraction(enemy.Confidence, 0.0);
			sanitized.SecondsSinceObservation = NonNegative(enemy.SecondsSinceObservation,
				std::numeric_limits<double>::max());
			sanitized.EstimatedHealthFraction = UnitFraction(enemy.EstimatedHealthFraction, 1.0);
			sanitized.Visible = enemy.Visible;
			sanitized.HasLineOfSight = enemy.Visible && enemy.HasLineOfSight;
			sanitized.FiringAtBot = enemy.FiringAtBot;
			observation.Enemies.push_back(std::move(sanitized));
		}

		observation.Items.reserve(snapshot.Items.size());
		for (const RuntimeItemSnapshot& item : snapshot.Items)
		{
			if (item.StableIdentity.empty() || item.Deleted || !IsFinite(item.Position))
				continue;

			ItemObservation sanitized;
			sanitized.Identity = item.StableIdentity;
			sanitized.Position = item.Position;
			sanitized.Distance = FiniteDistance(snapshot.Self.Position, item.Position);
			sanitized.Utility = FiniteOr(item.Utility, 0.0);
			sanitized.RouteDanger = NonNegative(item.RouteDanger);
			sanitized.Reachable = item.Reachable && std::isfinite(item.RouteDanger);
			observation.Items.push_back(std::move(sanitized));
		}

		std::sort(observation.Enemies.begin(), observation.Enemies.end(), EnemyOrder);
		std::sort(observation.Items.begin(), observation.Items.end(), ItemOrder);
		return observation;
	}
}
