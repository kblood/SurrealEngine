#include "BotBenchmarkAiFrameTiming.h"

#include <algorithm>

void BotBenchmarkAiFrameTiming::AddSampleMicroseconds(uint64_t microseconds)
{
	SampleCount++;
	MaxMicroseconds = std::max(MaxMicroseconds, microseconds);
	if (microseconds > MaximumTrackedMicroseconds)
	{
		HistogramBucketOverflowsExact++;
		return;
	}
	Histogram[static_cast<size_t>(microseconds)]++;
}

BotBenchmarkAiFrameTimingSummary BotBenchmarkAiFrameTiming::GetSummary() const
{
	return { SampleCount, HistogramBucketOverflowsExact, MaxMicroseconds,
		Percentile(50, 100), Percentile(95, 100), Percentile(99, 100) };
}

std::optional<uint64_t> BotBenchmarkAiFrameTiming::Percentile(uint64_t numerator,
	uint64_t denominator) const
{
	if (SampleCount == 0)
		return {};
	const uint64_t rank = (SampleCount * numerator + denominator - 1) / denominator;
	uint64_t observed = 0;
	for (size_t microseconds = 0; microseconds < Histogram.size(); microseconds++)
	{
		observed += Histogram[microseconds];
		if (observed >= rank)
			return static_cast<uint64_t>(microseconds);
	}
	return {};
}
