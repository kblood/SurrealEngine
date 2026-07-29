#include "BotAI/FallingHazardRecoveryGate.h"

#include <cmath>
#include <iostream>
#include <limits>
#include <string>

using namespace BotAI;

namespace
{
	int Failures = 0;

	void Check(bool condition, const std::string& message)
	{
		if (!condition)
		{
			std::cerr << "FAILED: " << message << '\n';
			Failures++;
		}
	}

	FallingHazardRecoveryEntry ValidEntry(uint64_t life = 1,
		uint64_t fall = 1)
	{
		FallingHazardRecoveryEntry entry;
		entry.LifeId = life;
		entry.FallEpisodeId = fall;
		entry.PrefixPromoted = true;
		entry.AnchorKnown = true;
		entry.AnchorX = 0.0;
		entry.AnchorY = 0.0;
		entry.CurrentX = 16.0;
		entry.CurrentY = 0.0;
		return entry;
	}

	void TestFailsClosed()
	{
		Check(IsValidFallingHazardRecoveryEntry(ValidEntry()),
			"a promoted finite anchor in range is eligible");
		auto invalid = [](FallingHazardRecoveryEntry entry,
			const std::string& message)
		{
			Check(!IsValidFallingHazardRecoveryEntry(entry), message);
		};
		auto entry = ValidEntry();
		entry.LifeId = 0;
		invalid(entry, "missing life fails closed");
		entry = ValidEntry();
		entry.FallEpisodeId = 0;
		invalid(entry, "missing fall episode fails closed");
		entry = ValidEntry();
		entry.PrefixPromoted = false;
		invalid(entry, "unpromoted prefix fails closed");
		entry = ValidEntry();
		entry.AnchorKnown = false;
		invalid(entry, "unknown anchor fails closed");
		entry = ValidEntry();
		entry.AnchorX = std::numeric_limits<double>::quiet_NaN();
		invalid(entry, "non-finite anchor fails closed");
		entry = ValidEntry();
		entry.CurrentY = std::numeric_limits<double>::infinity();
		invalid(entry, "non-finite current point fails closed");
		entry = ValidEntry();
		entry.CurrentX = FallingHazardRecoveryMinimumAnchorDistance - 0.01;
		invalid(entry, "too-near anchor fails closed");
		entry = ValidEntry();
		entry.CurrentX = FallingHazardRecoveryMaximumAnchorDistance + 0.01;
		invalid(entry, "too-distant anchor fails closed");
	}

	void TestOneAuthorizationPerFall()
	{
		FallingHazardRecoveryGate gate;
		const auto entry = ValidEntry(7, 11);
		Check(gate.Evaluate(entry) == FallingHazardRecoveryEligibility::Authorized,
			"first promoted fall is authorized");
		Check(gate.Evaluate(entry) == FallingHazardRecoveryEligibility::Debounced,
			"same fall is debounced");
		auto invalid = entry;
		invalid.PrefixPromoted = false;
		Check(gate.Evaluate(invalid) == FallingHazardRecoveryEligibility::Ineligible,
			"invalid repeat is rejected without reopening the fall");
		Check(gate.Evaluate(entry) == FallingHazardRecoveryEligibility::Debounced,
			"invalid repeat does not reopen a prior authorization");
		Check(gate.Evaluate(ValidEntry(7, 12))
			== FallingHazardRecoveryEligibility::Authorized,
			"new fall is independently authorized");
		gate.Reset();
		Check(gate.Evaluate(entry) == FallingHazardRecoveryEligibility::Authorized,
			"reset permits the same fall again");
	}
}

int main()
{
	TestFailsClosed();
	TestOneAuthorizationPerFall();
	if (Failures == 0)
		std::cout << "Falling hazard recovery gate tests passed\n";
	return Failures == 0 ? 0 : 1;
}
