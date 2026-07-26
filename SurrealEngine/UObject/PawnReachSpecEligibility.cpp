#include "PawnReachSpecEligibility.h"

namespace PawnMovement
{
	uint32_t UE1ReachSpecCapabilityMask(const ReachSpecCapabilityProfile& profile)
	{
		return (profile.CanWalk ? UE1ReachSpecFlagMask(UE1ReachSpecFlag::Walk) : 0u) |
			(profile.CanFly ? UE1ReachSpecFlagMask(UE1ReachSpecFlag::Fly) : 0u) |
			(profile.CanSwim ? UE1ReachSpecFlagMask(UE1ReachSpecFlag::Swim) : 0u) |
			(profile.CanJump ? UE1ReachSpecFlagMask(UE1ReachSpecFlag::Jump) : 0u) |
			(profile.CanOpenDoors ? UE1ReachSpecFlagMask(UE1ReachSpecFlag::Door) : 0u) |
			(profile.CanDoSpecial ? UE1ReachSpecFlagMask(UE1ReachSpecFlag::Special) : 0u) |
			(profile.IsPlayer ? UE1ReachSpecFlagMask(UE1ReachSpecFlag::PlayerOnly) : 0u);
	}

	ReachSpecEligibility EvaluateReachSpecEligibility(
		uint32_t reachFlags, const ReachSpecCapabilityProfile& profile)
	{
		ReachSpecEligibility result;
		result.RequiredFlags = reachFlags & UE1KnownReachSpecFlags;
		result.InvalidFlags = reachFlags & ~UE1KnownReachSpecFlags;
		result.CapabilityFlags = UE1ReachSpecCapabilityMask(profile);

		// An unknown bit makes the serialized requirement ambiguous. A zero flag
		// similarly provides no navigational contract. Both are rejected here so a
		// future integration cannot silently select an unmodelled path.
		if (result.InvalidFlags != 0u)
		{
			result.Disposition = ReachSpecEligibilityDisposition::InvalidFlags;
			return result;
		}
		if (result.RequiredFlags == 0u)
		{
			result.Disposition = ReachSpecEligibilityDisposition::NoRequirements;
			return result;
		}

		result.MissingFlags = result.RequiredFlags & ~result.CapabilityFlags;
		if (result.MissingFlags != 0u)
		{
			result.Disposition = ReachSpecEligibilityDisposition::MissingCapabilities;
			return result;
		}

		result.Eligible = true;
		result.Disposition = ReachSpecEligibilityDisposition::Eligible;
		return result;
	}

	ReachSpecCapabilityProfile UT436BotReachSpecProfile()
	{
		return { true, false, true, true, true, true, true };
	}

	ReachSpecCapabilityProfile UnrealBotReachSpecProfile()
	{
		return { true, false, true, true, true, true, true };
	}
}
