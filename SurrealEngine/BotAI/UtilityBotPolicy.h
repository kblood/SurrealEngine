#pragma once

#include "BotPolicy.h"

#include <cstdint>
#include <string>
#include <vector>

namespace BotAI
{
	class UtilityBotPolicy final : public Policy
	{
	public:
		const char* GetId() const override;
		uint32_t GetVersion() const override;
		void Reset(uint64_t seed) override;
		Decision Tick(const Observation& observation) override;

	private:
		struct ActionFrame
		{
			Action Selected = Action::Idle;
			std::string TargetIdentity;
		};

		std::vector<ActionFrame> ActionStack;
	};
}
