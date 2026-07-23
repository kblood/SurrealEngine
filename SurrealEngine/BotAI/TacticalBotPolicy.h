#pragma once

#include "BotPolicy.h"

#include <string>

namespace BotAI
{
	enum class TacticalState
	{
		IdleExplore,
		AcquireAttack,
		HuntRememberedEnemy,
		InvestigateUncertainObservation,
		ResupplyRetreat,
		UnstuckRecovery
	};

	class TacticalBotPolicy final : public Policy
	{
	public:
		const char* GetId() const override;
		uint32_t GetVersion() const override;
		void Reset(uint64_t seed) override;
		Decision Tick(const Observation& observation) override;

		TacticalState GetState() const { return State; }

	private:
		struct RememberedEnemy
		{
			std::string Identity;
			Vector3 Position;
			double Confidence = 0.0;
			double ExpiresAtSeconds = 0.0;
			bool Valid = false;
		};

		TacticalState State = TacticalState::IdleExplore;
		RememberedEnemy EnemyMemory;
		double ElapsedSeconds = 0.0;
		double RecoveryClearSeconds = 0.0;
	};
}
