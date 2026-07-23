#include "BrowserRelativeMouse.h"
#include <bit>

uint64_t BrowserRelativeMouseAccumulator::Pack(int32_t x, int32_t y) noexcept
{
	return static_cast<uint64_t>(static_cast<uint32_t>(x)) |
		(static_cast<uint64_t>(static_cast<uint32_t>(y)) << 32);
}

BrowserRelativeMouseDelta BrowserRelativeMouseAccumulator::Unpack(uint64_t value) noexcept
{
	return {
		std::bit_cast<int32_t>(static_cast<uint32_t>(value)),
		std::bit_cast<int32_t>(static_cast<uint32_t>(value >> 32))
	};
}

void BrowserRelativeMouseAccumulator::Add(int32_t dx, int32_t dy) noexcept
{
	uint64_t current = PackedDelta.load(std::memory_order_relaxed);
	for (;;)
	{
		const BrowserRelativeMouseDelta delta = Unpack(current);
		const uint32_t nextX = static_cast<uint32_t>(delta.X) + static_cast<uint32_t>(dx);
		const uint32_t nextY = static_cast<uint32_t>(delta.Y) + static_cast<uint32_t>(dy);
		const uint64_t next = Pack(std::bit_cast<int32_t>(nextX), std::bit_cast<int32_t>(nextY));
		if (PackedDelta.compare_exchange_weak(current, next,
			std::memory_order_release, std::memory_order_relaxed))
			return;
	}
}

BrowserRelativeMouseDelta BrowserRelativeMouseAccumulator::Drain() noexcept
{
	return Unpack(PackedDelta.exchange(0, std::memory_order_acq_rel));
}

void BrowserRelativeMouseAccumulator::Reset() noexcept
{
	PackedDelta.store(0, std::memory_order_release);
}
