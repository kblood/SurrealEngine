#pragma once

#include <atomic>
#include <cstdint>

struct BrowserRelativeMouseDelta
{
	int32_t X = 0;
	int32_t Y = 0;
};

class BrowserRelativeMouseAccumulator
{
public:
	void Add(int32_t dx, int32_t dy) noexcept;
	BrowserRelativeMouseDelta Drain() noexcept;
	void Reset() noexcept;

private:
	static uint64_t Pack(int32_t x, int32_t y) noexcept;
	static BrowserRelativeMouseDelta Unpack(uint64_t value) noexcept;

	std::atomic<uint64_t> PackedDelta = 0;
};
