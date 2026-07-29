#include "BotAI/HarmfulZoneEscapeGate.h"

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

static HarmfulZoneEntry HarmfulEntry(uint64_t lifeId, uint64_t episodeId)
{
	HarmfulZoneEntry entry;
	entry.LifeId = lifeId;
	entry.EpisodeId = episodeId;
	entry.IsPainZone = true;
	entry.DamagePerSecond = 1.0;
	return entry;
}

static void TestExactPositiveDamageRequirement()
{
	HarmfulZoneEntry entry = HarmfulEntry(1, 1);
	Check(HasExactPositiveDamagePerSecond(entry), "finite positive pain-zone damage qualifies");

	entry.DamagePerSecond = 0.0;
	Check(!HasExactPositiveDamagePerSecond(entry), "zero damage does not qualify");
	entry.DamagePerSecond = -1.0;
	Check(!HasExactPositiveDamagePerSecond(entry), "negative damage does not qualify");
	entry.DamagePerSecond = std::numeric_limits<double>::quiet_NaN();
	Check(!HasExactPositiveDamagePerSecond(entry), "NaN damage does not qualify");
	entry.DamagePerSecond = std::numeric_limits<double>::infinity();
	Check(!HasExactPositiveDamagePerSecond(entry), "infinite damage does not qualify");
	entry.DamagePerSecond = 1.0;
	entry.IsPainZone = false;
	Check(!HasExactPositiveDamagePerSecond(entry), "positive damage outside a pain zone does not qualify");
}

static void TestBoundaryDebounce()
{
	HarmfulZoneEscapeGate gate;
	const HarmfulZoneEntry entry = HarmfulEntry(7, 11);
	Check(gate.Evaluate(entry) == HarmfulZoneEscapeEligibility::Authorized, "first harmful entry is authorized");
	Check(gate.Evaluate(entry) == HarmfulZoneEscapeEligibility::Debounced, "repeated same boundary is debounced");

	HarmfulZoneEntry invalid = entry;
	invalid.DamagePerSecond = 0.0;
	Check(gate.Evaluate(invalid) == HarmfulZoneEscapeEligibility::Ineligible, "invalid observations do not authorize an escape");
	Check(gate.Evaluate(entry) == HarmfulZoneEscapeEligibility::Debounced, "invalid observation does not reopen an existing episode");
}

static void TestNewBoundaryAndLifeAuthorizeAgain()
{
	HarmfulZoneEscapeGate gate;
	Check(gate.Evaluate(HarmfulEntry(7, 11)) == HarmfulZoneEscapeEligibility::Authorized, "initial episode is authorized");
	Check(gate.Evaluate(HarmfulEntry(7, 12)) == HarmfulZoneEscapeEligibility::Authorized, "a later entry boundary authorizes once");
	Check(gate.Evaluate(HarmfulEntry(8, 12)) == HarmfulZoneEscapeEligibility::Authorized, "a new life does not inherit the prior debounce");
	Check(gate.Evaluate(HarmfulEntry(8, 12)) == HarmfulZoneEscapeEligibility::Debounced, "new-life episode remains debounced after authorization");
}

static void TestUnknownIdentityFailsClosedAndResetClearsState()
{
	HarmfulZoneEscapeGate gate;
	Check(gate.Evaluate(HarmfulEntry(0, 1)) == HarmfulZoneEscapeEligibility::Ineligible, "unknown life identity fails closed");
	Check(gate.Evaluate(HarmfulEntry(1, 0)) == HarmfulZoneEscapeEligibility::Ineligible, "unknown boundary identity fails closed");
	Check(gate.Evaluate(HarmfulEntry(1, 1)) == HarmfulZoneEscapeEligibility::Authorized, "known identities authorize the first entry");
	gate.Reset();
	Check(gate.Evaluate(HarmfulEntry(1, 1)) == HarmfulZoneEscapeEligibility::Authorized, "reset clears the episode debounce");
}

int main()
{
	TestExactPositiveDamageRequirement();
	TestBoundaryDebounce();
	TestNewBoundaryAndLifeAuthorizeAgain();
	TestUnknownIdentityFailsClosedAndResetClearsState();
	if (Failures == 0)
		std::cout << "Harmful zone escape gate tests passed\n";
	return Failures == 0 ? 0 : 1;
}
