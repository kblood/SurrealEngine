#include "BotBenchmark/BotBenchmarkProtocol.h"

#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

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
	const BotBenchmarkRunConfig defaults = BotBenchmarkRunConfig::Parse({}, {}, {}, {}, {}, {});
	if (defaults.GetSeed() != 104729 || defaults.GetMaxTicks() != 600
		|| defaults.GetDifficulty() != 3 || defaults.GetURL().empty()
		|| defaults.GetRoster().GetCount() != 1
		|| defaults.GetRoster().GetParticipants()[0].ExternalSkill != 3
		|| !defaults.GetRoster().GetParticipants()[0].RequestedName.empty()
		|| defaults.IsFailedNavigationAvoidanceEnabled()
		|| defaults.IsTargetlessMoveToTimeoutEnabled()
		|| defaults.IsTargetSelectionObserverEnabled()
		|| defaults.IsPickTargetObserverEnabled()
		|| defaults.IsWarnTargetObserverEnabled()
		|| defaults.IsInventoryMarkerDirectReachSafetyEnabled()
		|| defaults.IsNativePathCommitObserverEnabled()
		|| defaults.IsReachSpecCapabilityObserverEnabled()
		|| defaults.IsDirectReachCommandObserverEnabled()
		|| defaults.IsMovementCommandProvenanceObserverEnabled()
		|| defaults.IsPawnVisionConeEnabled()
		|| defaults.IsPawnVisionObserverEnabled()
		|| defaults.IsVectorNonFiniteObserverEnabled()
		|| defaults.IsFiniteMoveCommandGuardEnabled()
		|| defaults.IsWalkingHitWallMinHitWallCandidateEnabled())
		return Fail("default bot benchmark configuration or roster was incorrect");
	if (defaults.GetShadowPolicySet() != std::vector<std::string>{ "tactical-state", "utility-arena" })
		return Fail("default shadow policy set was not stable and canonical");

	const BotBenchmarkRunConfig parsed = BotBenchmarkRunConfig::Parse(
		"DM-Test?Game=Botpack.DeathMatchPlus", "evidence", "18446744073709551615", "72", "0.02", "7",
		std::string("2"), std::string("7,4"), std::string(" Loque,Tamerlane "));
	if (parsed.GetSeed() != UINT64_MAX || parsed.GetMaxTicks() != 72
		|| parsed.GetFixedDelta() != 0.02f || parsed.GetDifficulty() != 7
		|| parsed.GetRoster().GetCount() != 2
		|| parsed.GetRoster().GetParticipants()[0].RequestedName != "Loque"
		|| parsed.GetRoster().GetParticipants()[1].ExternalSkill != 4)
		return Fail("explicit bot benchmark configuration or roster was incorrect");

	bool rejectedDifficulty = false;
	try
	{
		BotBenchmarkRunConfig::Parse({}, {}, {}, {}, {}, "8");
	}
	catch (const std::invalid_argument&)
	{
		rejectedDifficulty = true;
	}
	if (!rejectedDifficulty)
		return Fail("invalid bot difficulty was accepted");

	bool rejectedTicks = false;
	try
	{
		BotBenchmarkRunConfig::Parse({}, {}, {}, "0", {}, {});
	}
	catch (const std::invalid_argument&)
	{
		rejectedTicks = true;
	}
	if (!rejectedTicks)
		return Fail("zero bot benchmark ticks were accepted");

	bool rejectedNegativeSeed = false;
	try
	{
		BotBenchmarkRunConfig::Parse({}, {}, "-1", {}, {}, {});
	}
	catch (const std::invalid_argument&)
	{
		rejectedNegativeSeed = true;
	}
	if (!rejectedNegativeSeed)
		return Fail("negative bot benchmark seed was accepted");

	bool rejectedRosterMismatch = false;
	try
	{
		BotBenchmarkRunConfig::Parse({}, {}, {}, {}, {}, "3",
			std::string("2"), std::string("7"), std::string("Loque,Tamerlane"));
	}
	catch (const std::invalid_argument&)
	{
		rejectedRosterMismatch = true;
	}
	if (!rejectedRosterMismatch)
		return Fail("run configuration accepted a roster/list mismatch");

	bool rejectedUncoupledWarnTargetObserver = false;
	try
	{
		BotBenchmarkRunConfig::Parse({}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {},
			{}, {}, {}, {}, {}, {}, {}, {}, std::string("1"));
	}
	catch (const std::invalid_argument&)
	{
		rejectedUncoupledWarnTargetObserver = true;
	}
	if (!rejectedUncoupledWarnTargetObserver)
		return Fail("WarnTarget observer was accepted without PickTarget observer");

	bool rejectedUncoupledReachSpecCapabilityObserver = false;
	try
	{
		BotBenchmarkRunConfig::Parse(
			{}, {}, {}, {}, {}, {},
			{}, {}, {},
			{}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {},
			{}, {}, {}, {},
			std::string("1"));
	}
	catch (const std::invalid_argument&)
	{
		rejectedUncoupledReachSpecCapabilityObserver = true;
	}
	if (!rejectedUncoupledReachSpecCapabilityObserver)
		return Fail("ReachSpec capability observer was accepted without native path commits");

	std::vector<BotBenchmarkActualParticipant> actualRoster = {
		{ 1, "pri:2", "Bot2", "Tamer\"lane", "Botpack.Bot" },
		{ 0, "pri:1", "Bot1", "Line\nBreak", "Botpack.Bot" }
	};
	const BotBenchmarkBuildIdentity buildIdentity{
		"0123456789abcdef0123456789abcdef01234567",
		"89abcdef0123456789abcdef0123456789abcdef",
		false, "SurrealEngine.exe", 123, std::string(64, 'A'),
		"sha256:" + std::string(64, 'B') };
	const BotBenchmarkRunSummary summary("failed", 2, 12, 0.24,
		"UT\n99", "436", "DM-Test", "controlled failure", std::move(actualRoster),
		{ 12, 0, 2400, 800, 1900, 2300 }, {}, buildIdentity);
	const std::string expectedSummary =
		"{\n"
		"  \"schema\": \"surreal-bot-benchmark-summary-v4\",\n"
		"  \"status\": \"failed\",\n"
		"  \"exit_code\": 2,\n"
		"  \"ticks\": \"12\",\n"
		"  \"simulated_seconds\": 0.240000000,\n"
		"  \"game\": \"UT\\n99\",\n"
		"  \"version\": \"436\",\n"
		"  \"map\": \"DM-Test\",\n"
		"  \"failure_reason\": \"controlled failure\",\n"
		"  \"requested_roster\": [\n"
		"    {\"roster_index\": 0, \"requested_name\": \"Loque\", \"external_skill\": 7, \"identity_fragment\": \"participant-v1:index=0;external_skill=7;requested_name_hex=4c6f717565\"},\n"
		"    {\"roster_index\": 1, \"requested_name\": \"Tamerlane\", \"external_skill\": 4, \"identity_fragment\": \"participant-v1:index=1;external_skill=4;requested_name_hex=54616d65726c616e65\"}\n"
		"  ],\n"
		"  \"actual_roster\": [\n"
		"    {\"roster_index\": 0, \"identity\": \"pri:1\", \"actor\": \"Bot1\", \"player_name\": \"Line\\nBreak\", \"class\": \"Botpack.Bot\"},\n"
		"    {\"roster_index\": 1, \"identity\": \"pri:2\", \"actor\": \"Bot2\", \"player_name\": \"Tamer\\\"lane\", \"class\": \"Botpack.Bot\"}\n"
		"  ],\n"
		"  \"ai_frame_timing\": {\n"
		"    \"schema\": \"surreal-bot-ai-frame-timing-v1\",\n"
		"    \"scope\": \"benchmark_observation_policy_driver_sampling\",\n"
		"    \"clock\": \"host_steady_clock_performance_only\",\n"
		"    \"behavioral_determinism\": \"not_behavioral_evidence\",\n"
		"    \"sample_count\": \"12\",\n"
		"    \"histogram_bucket_overflows_exact\": \"0\",\n"
		"    \"bucket_max_microseconds\": \"10000\",\n"
		"    \"p50_microseconds\": 800,\n"
		"    \"p95_microseconds\": 1900,\n"
		"    \"p99_microseconds\": 2300,\n"
		"    \"max_microseconds\": \"2400\",\n"
		"    \"components\": {\n"
		"      \"navigation_coverage\": {\n"
		"        \"sample_count\": \"0\",\n"
		"        \"histogram_bucket_overflows_exact\": \"0\",\n"
		"        \"p50_microseconds\": null,\n"
		"        \"p95_microseconds\": null,\n"
		"        \"p99_microseconds\": null,\n"
		"        \"max_microseconds\": \"0\"\n"
		"      },\n"
		"      \"shadow_observation_and_policy\": {\n"
		"        \"sample_count\": \"0\",\n"
		"        \"histogram_bucket_overflows_exact\": \"0\",\n"
		"        \"p50_microseconds\": null,\n"
		"        \"p95_microseconds\": null,\n"
		"        \"p99_microseconds\": null,\n"
		"        \"max_microseconds\": \"0\"\n"
		"      },\n"
		"      \"state_sampling\": {\n"
		"        \"sample_count\": \"0\",\n"
		"        \"histogram_bucket_overflows_exact\": \"0\",\n"
		"        \"p50_microseconds\": null,\n"
		"        \"p95_microseconds\": null,\n"
		"        \"p99_microseconds\": null,\n"
		"        \"max_microseconds\": \"0\"\n"
		"      }\n"
		"    }\n"
		"  },\n"
		"  \"config\": {\n"
		"    \"url\": \"DM-Test?Game=Botpack.DeathMatchPlus\",\n"
		"    \"output_directory\": \"evidence\",\n"
		"    \"seed\": \"18446744073709551615\",\n"
		"    \"max_ticks\": \"72\",\n"
		"    \"fixed_delta\": 0.020000000,\n"
		"    \"difficulty\": 7,\n"
		"    \"bot_count\": 2,\n"
		"    \"shadow_policy_set\": [\"tactical-state\", \"utility-arena\"],\n"
		"    \"harmful_zone_escape_enabled\": false,\n"
		"    \"walking_preflight_positive_dps_veto_enabled\": false,\n"
		"    \"hazard_swim_egress_enabled\": false,\n"
		"    \"hazard_swim_egress_live_enabled\": false,\n"
		"    \"failed_navigation_avoidance_enabled\": false,\n"
		"    \"falling_hazard_recovery_enabled\": false,\n"
		"    \"falling_hazard_recovery_live_enabled\": false,\n"
		"    \"targetless_move_to_timeout_enabled\": false,\n"
		"    \"direct_actor_move_toward_timeout_enabled\": false,\n"
		"    \"target_selection_observer_enabled\": false,\n"
		"    \"pick_target_observer_enabled\": false,\n"
		"    \"warn_target_observer_enabled\": false,\n"
		"    \"inventory_direct_reach_support_observer_enabled\": false,\n"
		"    \"inventory_marker_direct_reach_safety_enabled\": false,\n"
		"    \"native_path_commit_observer_enabled\": false,\n"
		"    \"reachspec_capability_observer_enabled\": false,\n"
		"    \"direct_reach_command_observer_enabled\": false,\n"
		"    \"movement_command_provenance_observer_enabled\": false,\n"
		"    \"hazard_residence_command_transition_ledger_observer_enabled\": false,\n"
		"    \"pawn_vision_cone_enabled\": false,\n"
		"    \"pawn_vision_observer_enabled\": false,\n"
		"    \"vector_nonfinite_observer_enabled\": false,\n"
		"    \"finite_move_command_guard_enabled\": false,\n"
		"    \"pick_reg_destination_zero_divide_guard_enabled\": false,\n"
		"    \"walking_hitwall_minhitwall_candidate_enabled\": false\n"
		"  }\n"
		"}\n";
	std::string serializedSummary = summary.ToJson(parsed);
	const size_t identityBegin = serializedSummary.find("  \"build_identity\": ");
	const size_t identityEnd = serializedSummary.find("  \"requested_roster\": ", identityBegin);
	if (identityBegin == std::string::npos || identityEnd == std::string::npos
		|| serializedSummary.find("surreal-engine-build-identity-v1", identityBegin) == std::string::npos)
		return Fail("summary build identity was not serialized");
	serializedSummary.erase(identityBegin, identityEnd - identityBegin);
	if (serializedSummary != expectedSummary)
		return Fail("v3 summary serialization, roster ordering, or escaping was not exact");

	bool rejectedDuplicateActualIndex = false;
	try
	{
		BotBenchmarkRunSummary duplicate("failed", 2, 0, 0.0, "UT", "436", "DM-Test", "failure",
			{ { 0, "pri:1", "Bot1", "A", "Botpack.Bot" },
			  { 0, "pri:2", "Bot2", "B", "Botpack.Bot" } });
		duplicate.ToJson(parsed);
	}
	catch (const std::invalid_argument&)
	{
		rejectedDuplicateActualIndex = true;
	}
	if (!rejectedDuplicateActualIndex)
		return Fail("summary accepted duplicate actual roster indexes");

	const BotBenchmarkRunSummary emptyActual("failed", 2, 0, 0.0,
		"UT", "436", {}, "setup failed", {});
	if (emptyActual.ToJson(defaults).find("  \"actual_roster\": [],\n") == std::string::npos)
		return Fail("failed setup did not serialize an explicit empty actual roster");

	return 0;
}
