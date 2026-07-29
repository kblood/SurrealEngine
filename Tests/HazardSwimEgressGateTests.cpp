#include "BotAI/HazardSwimEgressGate.h"

#include <cmath>
#include <iostream>
#include <limits>
#include <string>

using namespace BotAI;

static int Failures = 0;

static void Check(bool condition, const std::string& message)
{
	if (!condition)
	{
		std::cerr << "FAILED: " << message << '\n';
		Failures++;
	}
}

static HazardSwimEgressEntry ValidEntry(uint64_t lifeId = 1, uint64_t episodeId = 1)
{
	HazardSwimEgressEntry entry;
	entry.LifeId = lifeId;
	entry.EpisodeId = episodeId;
	entry.IsPainZone = true;
	entry.DamagePerSecond = 20.0;
	entry.AnchorKnown = true;
	entry.AnchorX = 128.0;
	entry.AnchorY = -64.0;
	entry.AnchorZ = 32.0;
	entry.StaticZone = true;
	entry.WaterZone = true;
	return entry;
}

static void TestFailsClosedForEveryRequiredFact()
{
	Check(IsValidHazardSwimEgressEntry(ValidEntry()), "complete exact entry qualifies");

	auto checkInvalid = [](HazardSwimEgressEntry entry, const std::string& message)
	{
		Check(!IsValidHazardSwimEgressEntry(entry), message);
	};

	HazardSwimEgressEntry entry = ValidEntry();
	entry.LifeId = 0;
	checkInvalid(entry, "unknown life fails closed");
	entry = ValidEntry();
	entry.EpisodeId = 0;
	checkInvalid(entry, "unknown episode fails closed");
	entry = ValidEntry();
	entry.IsPainZone = false;
	checkInvalid(entry, "non-pain zone fails closed");
	entry = ValidEntry();
	entry.DamagePerSecond = 0.0;
	checkInvalid(entry, "zero DPS fails closed");
	entry = ValidEntry();
	entry.DamagePerSecond = -1.0;
	checkInvalid(entry, "negative DPS fails closed");
	entry = ValidEntry();
	entry.DamagePerSecond = std::numeric_limits<double>::quiet_NaN();
	checkInvalid(entry, "NaN DPS fails closed");
	entry = ValidEntry();
	entry.DamagePerSecond = std::numeric_limits<double>::infinity();
	checkInvalid(entry, "infinite DPS fails closed");
	entry = ValidEntry();
	entry.AnchorKnown = false;
	checkInvalid(entry, "unknown anchor fails closed");
	entry = ValidEntry();
	entry.AnchorX = std::numeric_limits<double>::quiet_NaN();
	checkInvalid(entry, "non-finite X anchor fails closed");
	entry = ValidEntry();
	entry.AnchorY = std::numeric_limits<double>::infinity();
	checkInvalid(entry, "non-finite Y anchor fails closed");
	entry = ValidEntry();
	entry.AnchorZ = -std::numeric_limits<double>::infinity();
	checkInvalid(entry, "non-finite Z anchor fails closed");
	entry = ValidEntry();
	entry.StaticZone = false;
	checkInvalid(entry, "dynamic zone fails closed");
	entry = ValidEntry();
	entry.WaterZone = false;
	checkInvalid(entry, "non-water zone fails closed");
}

static void TestOneAuthorizationPerEpisode()
{
	HazardSwimEgressGate gate;
	const HazardSwimEgressEntry entry = ValidEntry(7, 11);
	Check(gate.Evaluate(entry) == HazardSwimEgressEligibility::Authorized,
		"first valid episode is authorized");
	Check(gate.Evaluate(entry) == HazardSwimEgressEligibility::Debounced,
		"same valid episode is debounced");

	HazardSwimEgressEntry invalid = entry;
	invalid.AnchorKnown = false;
	Check(gate.Evaluate(invalid) == HazardSwimEgressEligibility::Ineligible,
		"invalid repeat remains ineligible");
	Check(gate.Evaluate(entry) == HazardSwimEgressEligibility::Debounced,
		"invalid repeat does not reopen a prior episode");

	Check(gate.Evaluate(ValidEntry(7, 12)) == HazardSwimEgressEligibility::Authorized,
		"new episode authorizes once");
	Check(gate.Evaluate(ValidEntry(8, 12)) == HazardSwimEgressEligibility::Authorized,
		"new life authorizes once");
}

static void TestResetClearsDebounce()
{
	HazardSwimEgressGate gate;
	const HazardSwimEgressEntry entry = ValidEntry();
	Check(gate.Evaluate(entry) == HazardSwimEgressEligibility::Authorized,
		"initial authorization succeeds");
	gate.Reset();
	Check(gate.Evaluate(entry) == HazardSwimEgressEligibility::Authorized,
		"reset permits the same known episode");
}

int main()
{
	TestFailsClosedForEveryRequiredFact();
	TestOneAuthorizationPerEpisode();
	TestResetClearsDebounce();
	if (Failures == 0)
		std::cout << "Hazard swim egress gate tests passed\n";
	return Failures == 0 ? 0 : 1;
}
