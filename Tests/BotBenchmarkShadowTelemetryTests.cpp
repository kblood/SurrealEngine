#include "BotBenchmark/BotBenchmarkShadowTelemetry.h"

#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

namespace
{
	int Failures = 0;

	void Check(bool condition, const std::string& message)
	{
		if (!condition)
		{
			std::cerr << "FAILED: " << message << '\n';
			Failures++;
		}
	}

	BotAI::ShadowPolicySnapshot Decision(std::string id, uint64_t tick)
	{
		BotAI::ShadowPolicySnapshot snapshot;
		snapshot.PolicyId = std::move(id);
		snapshot.PolicyVersion = 1;
		snapshot.EvaluationCount = 1;
		snapshot.LatestObservationTick = tick;
		snapshot.HasDecision = true;
		snapshot.LatestDecision.Selected = BotAI::Action::Explore;
		snapshot.LatestDecision.Score = 20.0;
		snapshot.LatestDecision.Reason = "exploration";
		return snapshot;
	}

	template<typename Function>
	bool ThrowsInvalidArgument(Function function)
	{
		try
		{
			function();
		}
		catch (const std::invalid_argument&)
		{
			return true;
		}
		return false;
	}
}

int main()
{
	const std::string manifest = BotBenchmarkShadowTelemetry::ManifestJson(
		"fnv1a64:abc",
		600,
		{ { "utility-arena", 1 }, { "tactical-state", 1 } },
		{ { 1, "pri:2" }, { 0, "pri:1" } });
	const std::string expectedManifest =
		"{\n"
		"  \"schema\": \"surreal-bot-benchmark-shadow-manifest-v1\",\n"
		"  \"benchmark_config_id\": \"fnv1a64:abc\",\n"
		"  \"record_cap\": \"600\",\n"
		"  \"controls_live_bots\": false,\n"
		"  \"policies\": [\n"
		"    {\"id\": \"tactical-state\", \"version\": 1},\n"
		"    {\"id\": \"utility-arena\", \"version\": 1}\n"
		"  ],\n"
		"  \"participants\": [\n"
		"    {\"roster_index\": 0, \"identity\": \"pri:1\"},\n"
		"    {\"roster_index\": 1, \"identity\": \"pri:2\"}\n"
		"  ],\n"
		"  \"observation_limits\": {\"items\": false, \"armor\": false, \"stuck_time\": true}\n"
		"}\n";
	Check(manifest == expectedManifest, "shadow manifest is exact, sorted, bounded, and non-controlling");
	Check(BotBenchmarkShadowTelemetry::EventCap(600) == 600, "shadow output is capped at one record per tick");

	const std::string event = BotBenchmarkShadowTelemetry::EventJson(
		"fnv1a64:abc",
		4,
		9,
		{
			{ 1, "pri:2", false, {} },
			{ 0, "pri:1", true, { Decision("utility-arena", 9) } }
		});
	const std::string expectedEvent =
		"{\"schema\":\"surreal-bot-benchmark-shadow-event-v1\",\"seq\":\"4\","
		"\"benchmark_config_id\":\"fnv1a64:abc\",\"tick\":\"9\",\"participants\":["
		"{\"roster_index\":0,\"identity\":\"pri:1\",\"available\":true,\"shadow\":"
		"{\"schema\":\"surreal.bot-policy-shadow.v1\",\"policies\":["
		"{\"policyId\":\"utility-arena\",\"policyVersion\":1,\"evaluationCount\":1,"
		"\"actionTransitions\":0,\"latestObservationTick\":9,\"hasDecision\":true,"
		"\"selectedAction\":\"explore\",\"target\":\"\",\"score\":20,\"reason\":\"exploration\"}]}},"
		"{\"roster_index\":1,\"identity\":\"pri:2\",\"available\":false,\"shadow\":"
		"{\"schema\":\"surreal.bot-policy-shadow.v1\",\"policies\":[]}}]}\n";
	Check(event == expectedEvent, "shadow event is exact and roster ordered");

	Check(ThrowsInvalidArgument([]
	{
		BotBenchmarkShadowTelemetry::ManifestJson("id", 1, {}, { { 1, "pri:1" } });
	}), "non-contiguous manifest roster is rejected");
	Check(ThrowsInvalidArgument([]
	{
		BotBenchmarkShadowTelemetry::EventJson("id", 0, 1,
			{ { 0, "same", true, {} }, { 1, "same", true, {} } });
	}), "duplicate live identities are rejected");
	Check(ThrowsInvalidArgument([]
	{
		BotBenchmarkShadowTelemetry::ManifestJson("id", 1,
			{ { "duplicate", 1 }, { "duplicate", 2 } }, {});
	}), "duplicate policy IDs are rejected");
	Check(ThrowsInvalidArgument([]
	{
		auto invalid = Decision("utility-arena", 1);
		invalid.LatestDecision.Score = std::numeric_limits<double>::infinity();
		BotBenchmarkShadowTelemetry::EventJson("id", 0, 1,
			{ { 0, "pri:1", true, { invalid } } });
	}), "invalid nested policy telemetry is rejected");

	if (Failures == 0)
		std::cout << "All bot benchmark shadow telemetry tests passed.\n";
	return Failures == 0 ? 0 : 1;
}
