#include "BotBenchmark/BotBenchmarkAiFrameTiming.h"

#include <iostream>

namespace
{
	int Fail(const char* message)
	{
		std::cerr << message << '\n';
		return 1;
	}
}

int main()
{
	BotBenchmarkAiFrameTiming timing;
	if (timing.GetSummary().P50Microseconds.has_value())
		return Fail("empty timing histogram produced a percentile");

	for (uint64_t sample = 1; sample <= 100; sample++)
		timing.AddSampleMicroseconds(sample);
	const BotBenchmarkAiFrameTimingSummary summary = timing.GetSummary();
	if (summary.SampleCount != 100 || summary.HistogramBucketOverflowsExact != 0
		|| summary.MaxMicroseconds != 100 || summary.P50Microseconds != 50
		|| summary.P95Microseconds != 95 || summary.P99Microseconds != 99)
		return Fail("exact timing histogram percentile calculation was incorrect");

	BotBenchmarkAiFrameTiming overflow;
	for (uint64_t sample = 1; sample <= 94; sample++)
		overflow.AddSampleMicroseconds(sample);
	for (uint64_t index = 0; index < 6; index++)
		overflow.AddSampleMicroseconds(BotBenchmarkAiFrameTiming::MaximumTrackedMicroseconds + 1 + index);
	const BotBenchmarkAiFrameTimingSummary overflowSummary = overflow.GetSummary();
	if (overflowSummary.SampleCount != 100 || overflowSummary.HistogramBucketOverflowsExact != 6
		|| overflowSummary.MaxMicroseconds != BotBenchmarkAiFrameTiming::MaximumTrackedMicroseconds + 6
		|| overflowSummary.P50Microseconds != 50 || overflowSummary.P95Microseconds.has_value()
		|| overflowSummary.P99Microseconds.has_value())
		return Fail("overflow timing histogram did not fail closed for affected percentiles");

	return 0;
}
