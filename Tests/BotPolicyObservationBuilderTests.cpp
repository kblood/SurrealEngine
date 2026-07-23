#include "BotAI/BotPolicyObservationBuilder.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <string>
#include <utility>

using namespace BotAI;

static int failures = 0;

static void Check(bool condition, const std::string& message)
{
	if (!condition)
	{
		std::cerr << "FAILED: " << message << '\n';
		failures++;
	}
}

static RuntimeObservationSnapshot BaseSnapshot()
{
	RuntimeObservationSnapshot snapshot;
	snapshot.Self.StableIdentity = "self";
	snapshot.Self.Position = { 3.0, 4.0, 0.0 };
	return snapshot;
}

static RuntimeEnemySnapshot Enemy(std::string identity, Vector3 position)
{
	RuntimeEnemySnapshot enemy;
	enemy.StableIdentity = std::move(identity);
	enemy.Position = position;
	enemy.Hostile = true;
	enemy.Confidence = 1.0;
	return enemy;
}

static RuntimeItemSnapshot Item(std::string identity, Vector3 position)
{
	RuntimeItemSnapshot item;
	item.StableIdentity = std::move(identity);
	item.Position = position;
	item.Reachable = true;
	return item;
}

static bool SameVector(const Vector3& left, const Vector3& right)
{
	return left.X == right.X && left.Y == right.Y && left.Z == right.Z;
}

static bool SameObservation(const Observation& left, const Observation& right)
{
	if (left.Tick != right.Tick || left.DeltaSeconds != right.DeltaSeconds
		|| left.HealthFraction != right.HealthFraction || left.ArmorFraction != right.ArmorFraction
		|| left.AmmunitionFraction != right.AmmunitionFraction
		|| left.RecentIncomingDamage != right.RecentIncomingDamage || left.StuckSeconds != right.StuckSeconds
		|| left.ObjectiveUtility != right.ObjectiveUtility || left.HasUsableWeapon != right.HasUsableWeapon
		|| left.Enemies.size() != right.Enemies.size() || left.Items.size() != right.Items.size())
		return false;
	for (size_t index = 0; index < left.Enemies.size(); index++)
	{
		const EnemyObservation& a = left.Enemies[index];
		const EnemyObservation& b = right.Enemies[index];
		if (a.Identity != b.Identity || !SameVector(a.Position, b.Position) || !SameVector(a.Velocity, b.Velocity)
			|| a.Distance != b.Distance || a.Confidence != b.Confidence
			|| a.SecondsSinceObservation != b.SecondsSinceObservation
			|| a.EstimatedHealthFraction != b.EstimatedHealthFraction || a.Visible != b.Visible
			|| a.HasLineOfSight != b.HasLineOfSight || a.FiringAtBot != b.FiringAtBot)
			return false;
	}
	for (size_t index = 0; index < left.Items.size(); index++)
	{
		const ItemObservation& a = left.Items[index];
		const ItemObservation& b = right.Items[index];
		if (a.Identity != b.Identity || !SameVector(a.Position, b.Position) || a.Distance != b.Distance
			|| a.Utility != b.Utility || a.RouteDanger != b.RouteDanger || a.Reachable != b.Reachable)
			return false;
	}
	return true;
}

static void TestFiltering()
{
	RuntimeObservationSnapshot snapshot = BaseSnapshot();
	snapshot.Enemies.push_back(Enemy("self", { 1.0, 1.0, 1.0 }));
	auto deleted = Enemy("deleted", { 1.0, 1.0, 1.0 });
	deleted.Deleted = true;
	snapshot.Enemies.push_back(deleted);
	auto dead = Enemy("dead", { 1.0, 1.0, 1.0 });
	dead.Dead = true;
	snapshot.Enemies.push_back(dead);
	auto friendly = Enemy("friendly", { 1.0, 1.0, 1.0 });
	friendly.Hostile = false;
	snapshot.Enemies.push_back(friendly);
	snapshot.Enemies.push_back(Enemy("hostile", { 6.0, 8.0, 12.0 }));
	snapshot.Items.push_back(Item("valid-item", { 0.0, 0.0, 0.0 }));
	auto deletedItem = Item("deleted-item", { 0.0, 0.0, 0.0 });
	deletedItem.Deleted = true;
	snapshot.Items.push_back(deletedItem);

	Observation observation = PolicyObservationBuilder::Build(snapshot);
	Check(observation.Enemies.size() == 1 && observation.Enemies[0].Identity == "hostile", "self, deleted, dead, and non-hostile pawns are filtered");
	Check(observation.Items.size() == 1 && observation.Items[0].Identity == "valid-item", "deleted items are filtered");
}

static void TestDistancesAndResourceMapping()
{
	RuntimeObservationSnapshot snapshot = BaseSnapshot();
	snapshot.Tick = 42;
	snapshot.DeltaSeconds = 0.125;
	snapshot.ObjectiveUtility = 3.5;
	snapshot.Self.HealthFraction = 0.75;
	snapshot.Self.ArmorFraction = 0.5;
	snapshot.Self.AmmunitionFraction = 0.25;
	snapshot.Self.RecentIncomingDamage = 0.4;
	snapshot.Self.StuckSeconds = 1.25;
	snapshot.Self.HasUsableWeapon = true;
	snapshot.Enemies.push_back(Enemy("enemy", { 6.0, 8.0, 12.0 }));
	snapshot.Items.push_back(Item("item", { 3.0, 4.0, 5.0 }));

	Observation observation = PolicyObservationBuilder::Build(snapshot);
	Check(observation.Tick == 42 && observation.DeltaSeconds == 0.125 && observation.ObjectiveUtility == 3.5, "tick, delta, and objective data are preserved");
	Check(observation.HealthFraction == 0.75 && observation.ArmorFraction == 0.5
		&& observation.AmmunitionFraction == 0.25, "resource fractions map to the policy observation");
	Check(observation.RecentIncomingDamage == 0.4 && observation.StuckSeconds == 1.25
		&& observation.HasUsableWeapon, "damage, stuck, and weapon state map to the policy observation");
	Check(observation.Enemies[0].Distance == 13.0, "enemy distance is computed from self position");
	Check(observation.Items[0].Distance == 5.0, "item distance is computed from self position");
}

static void TestClampingAndNonfiniteValues()
{
	const double nan = std::numeric_limits<double>::quiet_NaN();
	const double infinity = std::numeric_limits<double>::infinity();
	RuntimeObservationSnapshot snapshot = BaseSnapshot();
	snapshot.DeltaSeconds = nan;
	snapshot.ObjectiveUtility = infinity;
	snapshot.Self.HealthFraction = 2.0;
	snapshot.Self.ArmorFraction = -1.0;
	snapshot.Self.AmmunitionFraction = nan;
	snapshot.Self.RecentIncomingDamage = -4.0;
	snapshot.Self.StuckSeconds = infinity;
	auto enemy = Enemy("enemy", { 5.0, 5.0, 5.0 });
	enemy.Velocity = { infinity, 1.0, 2.0 };
	enemy.Confidence = 2.0;
	enemy.SecondsSinceObservation = -3.0;
	enemy.EstimatedHealthFraction = nan;
	enemy.Visible = false;
	enemy.HasLineOfSight = true;
	snapshot.Enemies.push_back(enemy);
	snapshot.Enemies.push_back(Enemy("bad-position", { nan, 0.0, 0.0 }));
	auto item = Item("uncertain-item", { 1.0, 2.0, 3.0 });
	item.Utility = nan;
	item.RouteDanger = infinity;
	snapshot.Items.push_back(item);
	snapshot.Items.push_back(Item("", { 1.0, 2.0, 3.0 }));

	Observation observation = PolicyObservationBuilder::Build(snapshot);
	Check(observation.DeltaSeconds == 0.0 && observation.ObjectiveUtility == 0.0, "nonfinite frame metadata fails closed");
	Check(observation.HealthFraction == 1.0 && observation.ArmorFraction == 0.0
		&& observation.AmmunitionFraction == 0.0, "resource fractions clamp and nonfinite values fail closed");
	Check(observation.RecentIncomingDamage == 0.0 && observation.StuckSeconds == 0.0, "invalid hazard values do not invent damage or recovery");
	Check(observation.Enemies.size() == 1, "invalid enemy identity and position are rejected");
	Check(observation.Enemies[0].Confidence == 1.0 && observation.Enemies[0].SecondsSinceObservation == 0.0
		&& observation.Enemies[0].EstimatedHealthFraction == 1.0, "enemy metadata clamps to conservative finite values");
	Check(SameVector(observation.Enemies[0].Velocity, {}) && !observation.Enemies[0].HasLineOfSight, "invalid velocity is zeroed and LOS requires visibility");
	Check(observation.Items.size() == 1 && observation.Items[0].Utility == 0.0
		&& observation.Items[0].RouteDanger == 0.0 && !observation.Items[0].Reachable, "invalid item metadata disables acquisition safely");
	Check(std::isfinite(observation.Enemies[0].Distance) && std::isfinite(observation.Items[0].Distance), "all generated distances are finite");

	snapshot.Self.Position = { nan, 0.0, 0.0 };
	observation = PolicyObservationBuilder::Build(snapshot);
	Check(observation.Enemies.empty() && observation.Items.empty(), "invalid self position suppresses spatial records");
	snapshot.Self.Position = {};
	snapshot.Self.StableIdentity.clear();
	observation = PolicyObservationBuilder::Build(snapshot);
	Check(observation.Enemies.empty() && observation.Items.empty(), "missing self identity suppresses spatial records");
}

static void TestDeterministicOrderingAndPermutation()
{
	RuntimeObservationSnapshot first = BaseSnapshot();
	first.Tick = 9;
	first.Enemies = {
		Enemy("zulu", { 5.0, 1.0, 0.0 }),
		Enemy("alpha", { 2.0, 1.0, 0.0 }),
		Enemy("alpha", { 1.0, 1.0, 0.0 })
	};
	first.Items = {
		Item("zulu-item", { 5.0, 1.0, 0.0 }),
		Item("alpha-item", { 2.0, 1.0, 0.0 })
	};
	RuntimeObservationSnapshot second = first;
	std::reverse(second.Enemies.begin(), second.Enemies.end());
	std::reverse(second.Items.begin(), second.Items.end());

	const Observation firstObservation = PolicyObservationBuilder::Build(first);
	const Observation secondObservation = PolicyObservationBuilder::Build(second);
	Check(firstObservation.Enemies[0].Identity == "alpha" && firstObservation.Enemies[1].Identity == "alpha"
		&& firstObservation.Enemies[2].Identity == "zulu", "enemies are ordered by stable identity with deterministic duplicate ordering");
	Check(firstObservation.Items[0].Identity == "alpha-item" && firstObservation.Items[1].Identity == "zulu-item", "items are ordered by stable identity");
	Check(SameObservation(firstObservation, secondObservation), "permuted actor iteration produces an identical policy observation");
}

int main()
{
	TestFiltering();
	TestDistancesAndResourceMapping();
	TestClampingAndNonfiniteValues();
	TestDeterministicOrderingAndPermutation();
	if (failures == 0)
		std::cout << "All bot policy observation builder tests passed.\n";
	return failures == 0 ? 0 : 1;
}
