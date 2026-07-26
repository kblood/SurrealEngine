#pragma once

#include <cstdint>

namespace PawnMovement
{
	// UE1 Engine/Inc/UnPath.h ReachSpec flags. Keep these numeric values in
	// sync with the serialized map data and APawn::calcMoveFlags().
	enum class UE1ReachSpecFlag : uint32_t
	{
		Walk = 1u,
		Fly = 2u,
		Swim = 4u,
		Jump = 8u,
		Door = 16u,
		Special = 32u,
		PlayerOnly = 64u,
	};

	constexpr uint32_t UE1ReachSpecFlagMask(UE1ReachSpecFlag flag)
	{
		return static_cast<uint32_t>(flag);
	}

	inline constexpr uint32_t UE1KnownReachSpecFlags =
		UE1ReachSpecFlagMask(UE1ReachSpecFlag::Walk) |
		UE1ReachSpecFlagMask(UE1ReachSpecFlag::Fly) |
		UE1ReachSpecFlagMask(UE1ReachSpecFlag::Swim) |
		UE1ReachSpecFlagMask(UE1ReachSpecFlag::Jump) |
		UE1ReachSpecFlagMask(UE1ReachSpecFlag::Door) |
		UE1ReachSpecFlagMask(UE1ReachSpecFlag::Special) |
		UE1ReachSpecFlagMask(UE1ReachSpecFlag::PlayerOnly);

	// This is a deliberately engine-independent mirror of the Pawn fields used
	// by APawn::calcMoveFlags(). It is not connected to route selection yet.
	struct ReachSpecCapabilityProfile
	{
		bool CanWalk = false;
		bool CanFly = false;
		bool CanSwim = false;
		bool CanJump = false;
		bool CanOpenDoors = false;
		bool CanDoSpecial = false;
		bool IsPlayer = false;
	};

	enum class ReachSpecEligibilityDisposition
	{
		Eligible,
		NoRequirements,
		InvalidFlags,
		MissingCapabilities,
	};

	struct ReachSpecEligibility
	{
		bool Eligible = false;
		uint32_t RequiredFlags = 0;
		uint32_t CapabilityFlags = 0;
		uint32_t MissingFlags = 0;
		uint32_t InvalidFlags = 0;
		ReachSpecEligibilityDisposition Disposition = ReachSpecEligibilityDisposition::NoRequirements;
	};

	uint32_t UE1ReachSpecCapabilityMask(const ReachSpecCapabilityProfile& profile);
	ReachSpecEligibility EvaluateReachSpecEligibility(
		uint32_t reachFlags, const ReachSpecCapabilityProfile& profile);

	// Deterministic test presets reconstructed from Botpack/Bot.uc
	// PreSetMovement and UnrealShare/Bots.uc PreSetMovement. These are fixtures,
	// not live Pawn defaults: bCanJump may change while a bot is executing.
	ReachSpecCapabilityProfile UT436BotReachSpecProfile();
	ReachSpecCapabilityProfile UnrealBotReachSpecProfile();
}
