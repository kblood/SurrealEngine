#include "UObject/PawnReachSpecEligibility.h"

#include <iostream>
#include <string>

using PawnMovement::EvaluateReachSpecEligibility;
using PawnMovement::ReachSpecCapabilityProfile;
using PawnMovement::ReachSpecEligibilityDisposition;
using PawnMovement::UE1ReachSpecFlag;
using PawnMovement::UE1ReachSpecFlagMask;

namespace
{
	int Failures = 0;

	void Check(bool condition, const std::string& message)
	{
		if (!condition)
		{
			std::cerr << "FAILED: " << message << '\n';
			++Failures;
		}
	}

	void CheckEligible(uint32_t flags, const ReachSpecCapabilityProfile& profile,
		const std::string& message)
	{
		const auto result = EvaluateReachSpecEligibility(flags, profile);
		Check(result.Eligible, message);
		Check(result.Disposition == ReachSpecEligibilityDisposition::Eligible,
			message + " has the eligible disposition");
		Check(result.MissingFlags == 0u, message + " has no missing flags");
	}

	void CheckMissing(uint32_t flags, const ReachSpecCapabilityProfile& profile,
		uint32_t missingFlags, const std::string& message)
	{
		const auto result = EvaluateReachSpecEligibility(flags, profile);
		Check(!result.Eligible, message + " is rejected");
		Check(result.Disposition == ReachSpecEligibilityDisposition::MissingCapabilities,
			message + " reports missing capabilities");
		Check(result.MissingFlags == missingFlags, message + " reports the exact missing flags");
	}

	void TestCapabilityRequirements()
	{
		const ReachSpecCapabilityProfile allCapabilities { true, true, true, true, true, true, true };
		const ReachSpecCapabilityProfile walker { true, false, false, false, false, false, false };

		CheckEligible(UE1ReachSpecFlagMask(UE1ReachSpecFlag::Walk), allCapabilities, "walk requirement");
		CheckEligible(UE1ReachSpecFlagMask(UE1ReachSpecFlag::Jump), allCapabilities, "jump requirement");
		CheckEligible(UE1ReachSpecFlagMask(UE1ReachSpecFlag::Swim), allCapabilities, "swim requirement");
		CheckEligible(UE1ReachSpecFlagMask(UE1ReachSpecFlag::Fly), allCapabilities, "fly requirement");
		CheckEligible(UE1ReachSpecFlagMask(UE1ReachSpecFlag::Special), allCapabilities, "special requirement");
		CheckEligible(UE1ReachSpecFlagMask(UE1ReachSpecFlag::Door), allCapabilities, "door requirement");
		CheckEligible(UE1ReachSpecFlagMask(UE1ReachSpecFlag::PlayerOnly), allCapabilities, "player-only requirement");

		CheckEligible(
			UE1ReachSpecFlagMask(UE1ReachSpecFlag::Walk) |
			UE1ReachSpecFlagMask(UE1ReachSpecFlag::Jump) |
			UE1ReachSpecFlagMask(UE1ReachSpecFlag::Swim),
			allCapabilities, "walk/jump/swim combination");
		CheckEligible(
			UE1ReachSpecFlagMask(UE1ReachSpecFlag::Walk) |
			UE1ReachSpecFlagMask(UE1ReachSpecFlag::Fly) |
			UE1ReachSpecFlagMask(UE1ReachSpecFlag::Special),
			allCapabilities, "walk/fly/special combination");

		CheckEligible(UE1ReachSpecFlagMask(UE1ReachSpecFlag::Walk), walker, "walker uses a walk spec");
		CheckMissing(
			UE1ReachSpecFlagMask(UE1ReachSpecFlag::Walk) |
			UE1ReachSpecFlagMask(UE1ReachSpecFlag::Jump) |
			UE1ReachSpecFlagMask(UE1ReachSpecFlag::Swim),
			walker,
			UE1ReachSpecFlagMask(UE1ReachSpecFlag::Jump) |
			UE1ReachSpecFlagMask(UE1ReachSpecFlag::Swim),
			"combined requirements need every listed capability");
		CheckMissing(UE1ReachSpecFlagMask(UE1ReachSpecFlag::Jump), walker,
			UE1ReachSpecFlagMask(UE1ReachSpecFlag::Jump), "walker cannot use jump spec");
		CheckMissing(UE1ReachSpecFlagMask(UE1ReachSpecFlag::Swim), walker,
			UE1ReachSpecFlagMask(UE1ReachSpecFlag::Swim), "walker cannot use swim spec");
		CheckMissing(UE1ReachSpecFlagMask(UE1ReachSpecFlag::Fly), walker,
			UE1ReachSpecFlagMask(UE1ReachSpecFlag::Fly), "walker cannot use fly spec");
		CheckMissing(UE1ReachSpecFlagMask(UE1ReachSpecFlag::Special), walker,
			UE1ReachSpecFlagMask(UE1ReachSpecFlag::Special), "walker cannot use special spec");
	}

	void TestInvalidRequirementsFailClosed()
	{
		const ReachSpecCapabilityProfile allCapabilities { true, true, true, true, true, true, true };
		const auto noRequirements = EvaluateReachSpecEligibility(0u, allCapabilities);
		Check(!noRequirements.Eligible, "zero requirements fail closed");
		Check(noRequirements.Disposition == ReachSpecEligibilityDisposition::NoRequirements,
			"zero requirements are identified separately");

		const auto unknownOnly = EvaluateReachSpecEligibility(0x80u, allCapabilities);
		Check(!unknownOnly.Eligible, "unknown reach flag fails closed");
		Check(unknownOnly.Disposition == ReachSpecEligibilityDisposition::InvalidFlags,
			"unknown reach flag is invalid");
		Check(unknownOnly.InvalidFlags == 0x80u, "unknown bit is preserved for diagnostics");

		const auto unknownAndWalk = EvaluateReachSpecEligibility(
			UE1ReachSpecFlagMask(UE1ReachSpecFlag::Walk) | 0x80u, allCapabilities);
		Check(!unknownAndWalk.Eligible, "known and unknown reach flags fail closed together");
		Check(unknownAndWalk.InvalidFlags == 0x80u,
			"mixed known and unknown flags retain the unknown bit");
	}

	void TestRetailBotFixtures()
	{
		const auto ut436 = PawnMovement::UT436BotReachSpecProfile();
		const auto unreal = PawnMovement::UnrealBotReachSpecProfile();
		const uint32_t standardBotPath =
			UE1ReachSpecFlagMask(UE1ReachSpecFlag::Walk) |
			UE1ReachSpecFlagMask(UE1ReachSpecFlag::Jump) |
			UE1ReachSpecFlagMask(UE1ReachSpecFlag::Swim) |
			UE1ReachSpecFlagMask(UE1ReachSpecFlag::Door) |
			UE1ReachSpecFlagMask(UE1ReachSpecFlag::Special) |
			UE1ReachSpecFlagMask(UE1ReachSpecFlag::PlayerOnly);

		CheckEligible(standardBotPath, ut436, "UT436 Bot PreSetMovement fixture");
		CheckEligible(standardBotPath, unreal, "Unreal Bots PreSetMovement fixture");
		CheckMissing(UE1ReachSpecFlagMask(UE1ReachSpecFlag::Fly), ut436,
			UE1ReachSpecFlagMask(UE1ReachSpecFlag::Fly), "UT436 bot fixture rejects fly specs");
		CheckMissing(UE1ReachSpecFlagMask(UE1ReachSpecFlag::Fly), unreal,
			UE1ReachSpecFlagMask(UE1ReachSpecFlag::Fly), "Unreal bot fixture rejects fly specs");
	}
}

int main()
{
	TestCapabilityRequirements();
	TestInvalidRequirementsFailClosed();
	TestRetailBotFixtures();
	if (Failures == 0)
		std::cout << "Pawn ReachSpec eligibility tests passed\n";
	return Failures == 0 ? 0 : 1;
}
