#pragma once

#include "BotPolicy.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace BotAI
{
	constexpr size_t MaxMovementSafetyProbes = 16;

	enum class MovementProbeStatus
	{
		Viable,
		Invalid,
		Blocked,
		PainZone,
		UnsupportedLanding,
		ExcessiveDrop,
		UnsafeJump
	};

	enum class MovementAdviceAction
	{
		Hold,
		Continue,
		Steer,
		EscapeHazard,
		RecoverFromStuck
	};

	struct MovementSafetyContext
	{
		Vector3 DesiredDirection;
		Vector3 Velocity;
		double CollisionRadius = 1.0;
		double SafeDropDistance = 48.0;
		double StuckSeconds = 0.0;
		bool InPainZone = false;
	};

	// Geometry and zone queries remain an engine-adapter responsibility. Keeping
	// the result as data makes the advisor deterministic and safe to run in shadow.
	struct MovementProbe
	{
		uint32_t Index = 0;
		Vector3 Direction;
		double ForwardClearance = 0.0;
		double LandingDrop = 0.0;
		double ExpectedDamage = 0.0;
		bool HasLanding = true;
		bool EntersPainZone = false;
		bool RequiresJump = false;
		bool LandingReachable = true;
	};

	struct MovementAlternative
	{
		uint32_t ProbeIndex = 0;
		Vector3 Direction;
		MovementProbeStatus Status = MovementProbeStatus::Invalid;
		double Score = 0.0;
		std::string Reason;
	};

	struct MovementAdvice
	{
		MovementAdviceAction Action = MovementAdviceAction::Hold;
		uint32_t SelectedProbeIndex = 0;
		Vector3 Direction;
		double Score = 0.0;
		std::string Reason;
		bool ValidInput = true;
		std::vector<MovementAlternative> Alternatives;
	};

	class MovementSafetyAdvisor
	{
	public:
		void Reset();
		MovementAdvice Evaluate(const MovementSafetyContext& context, const std::vector<MovementProbe>& probes);

	private:
		bool HasRecoveryProbe = false;
		uint32_t LastRecoveryProbe = 0;
	};
}
