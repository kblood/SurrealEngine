#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

// Performance-only timing for the benchmark-owned observation/policy/sampling
// scope. Values are deliberately kept out of deterministic behavior checks:
// the accumulator is deterministic for supplied microsecond samples, while
// the supplied durations are host-performance observations.
struct BotBenchmarkAiFrameTimingSummary
{
	uint64_t SampleCount = 0;
	uint64_t HistogramBucketOverflowsExact = 0;
	uint64_t MaxMicroseconds = 0;
	std::optional<uint64_t> P50Microseconds;
	std::optional<uint64_t> P95Microseconds;
	std::optional<uint64_t> P99Microseconds;
};

class BotBenchmarkAiFrameTiming
{
public:
	// 10 ms covers the performance target with 1 us precision without an
	// unbounded sample reservoir. Percentiles whose rank falls in the explicit
	// overflow bucket are unavailable rather than silently clamped.
	static constexpr uint64_t MaximumTrackedMicroseconds = 10000;

	void AddSampleMicroseconds(uint64_t microseconds);
	BotBenchmarkAiFrameTimingSummary GetSummary() const;

private:
	std::optional<uint64_t> Percentile(uint64_t numerator, uint64_t denominator) const;

	std::array<uint64_t, MaximumTrackedMicroseconds + 1> Histogram{};
	uint64_t SampleCount = 0;
	uint64_t HistogramBucketOverflowsExact = 0;
	uint64_t MaxMicroseconds = 0;
};
