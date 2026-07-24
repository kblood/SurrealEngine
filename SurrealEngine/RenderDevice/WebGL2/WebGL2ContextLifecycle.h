#pragma once

#include <cstdint>

enum class WebGL2ContextState : uint8_t
{
	Uninitialized,
	Ready,
	Lost,
	Restoring,
	Failed
};

class WebGL2ContextLifecycle
{
public:
	bool CompleteInitialCreation(bool success);
	bool MarkLost();
	bool BeginRestore();
	bool CompleteRestore(bool success);

	WebGL2ContextState State() const { return state; }
	uint32_t Generation() const { return generation; }
	uint32_t LossCount() const { return lossCount; }

private:
	WebGL2ContextState state = WebGL2ContextState::Uninitialized;
	uint32_t generation = 0;
	uint32_t lossCount = 0;
};
