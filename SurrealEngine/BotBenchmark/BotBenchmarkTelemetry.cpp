#include "BotBenchmarkTelemetry.h"
#include "BotBenchmarkProtocol.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <locale>
#include <sstream>
#include <stdexcept>

namespace
{
	std::string EscapeJson(const std::string& value)
	{
		std::ostringstream out;
		out.imbue(std::locale::classic());
		for (unsigned char c : value)
		{
			switch (c)
			{
			case '"': out << "\\\""; break;
			case '\\': out << "\\\\"; break;
			case '\b': out << "\\b"; break;
			case '\f': out << "\\f"; break;
			case '\n': out << "\\n"; break;
			case '\r': out << "\\r"; break;
			case '\t': out << "\\t"; break;
			default:
				if (c < 0x20)
					out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(c) << std::dec;
				else
					out << static_cast<char>(c);
			}
		}
		return out.str();
	}

	std::string JsonString(const std::string& value)
	{
		return "\"" + EscapeJson(value) + "\"";
	}

	std::string Fixed(double value, int precision)
	{
		if (!std::isfinite(value))
			throw std::invalid_argument("bot benchmark telemetry contains a non-finite number");
		if (value == 0.0)
			value = 0.0;
		std::ostringstream out;
		out.imbue(std::locale::classic());
		out << std::fixed << std::setprecision(precision) << value;
		return out.str();
	}

	void Hash(uint64_t& value, const std::string& text)
	{
		for (unsigned char c : text)
		{
			value ^= c;
			value *= 1099511628211ULL;
		}
	}

	std::string Hex64(uint64_t value)
	{
		std::ostringstream out;
		out.imbue(std::locale::classic());
		out << std::hex << std::setw(16) << std::setfill('0') << value;
		return out.str();
	}

	const char* CollisionName(PawnMovement::WalkingStepCollisionKind collision)
	{
		using PawnMovement::WalkingStepCollisionKind;
		switch (collision)
		{
		case WalkingStepCollisionKind::Clear: return "clear";
		case WalkingStepCollisionKind::StaticBsp: return "static_bsp";
		case WalkingStepCollisionKind::Mover: return "mover";
		case WalkingStepCollisionKind::DynamicActor: return "dynamic_actor";
		default: return "unknown";
		}
	}

	const char* CollisionName(PawnMovement::FallingParityCollisionKind collision)
	{
		using PawnMovement::FallingParityCollisionKind;
		switch (collision)
		{
		case FallingParityCollisionKind::Clear: return "clear";
		case FallingParityCollisionKind::StaticWorld: return "static_world";
		case FallingParityCollisionKind::Mover: return "mover";
		case FallingParityCollisionKind::DynamicActor: return "dynamic_actor";
		default: return "unknown";
		}
	}

	const char* ZoneName(PawnMovement::WalkingStepZoneKind zone)
	{
		using PawnMovement::WalkingStepZoneKind;
		switch (zone)
		{
		case WalkingStepZoneKind::Safe: return "safe";
		case WalkingStepZoneKind::Pain: return "pain";
		case WalkingStepZoneKind::Water: return "water";
		default: return "unknown";
		}
	}

	void WriteVector(std::ostringstream& out, const vec3& value)
	{
		out << "{\"x\":" << Fixed(value.x, 6)
			<< ",\"y\":" << Fixed(value.y, 6)
			<< ",\"z\":" << Fixed(value.z, 6) << "}";
	}

	void WriteProbe(std::ostringstream& out,
		const PawnMovement::WalkingStepPreflightProbeDiagnostic& probe)
	{
		out << "{\"collision\":" << JsonString(CollisionName(probe.Collision))
			<< ",\"fraction\":" << Fixed(probe.Fraction, 9) << ",\"delta\":";
		WriteVector(out, probe.Delta);
		out << ",\"normal\":";
		WriteVector(out, probe.Normal);
		out << "}";
	}

	void WritePreflightDiagnostic(std::ostringstream& out,
		const PawnMovement::WalkingStepPreflightDiagnosticRecord& diagnostic)
	{
		const char* reason = PawnMovement::WalkingStepPreflightReasonMetricName(
			diagnostic.Reason);
		out << "{\"source_pawn_actor\":" << JsonString(diagnostic.SourcePawnActor)
			<< ",\"sequence\":\"" << diagnostic.Sequence
			<< "\",\"life_generation\":\"" << diagnostic.LifeGeneration
			<< "\",\"invocation_token\":\"" << diagnostic.InvocationToken
			<< "\",\"walking_iteration\":" << diagnostic.WalkingIteration
			<< ",\"phase\":" << JsonString(diagnostic.Phase)
			<< ",\"transition_outcome\":" << JsonString(diagnostic.TransitionOutcome)
			<< ",\"reason\":"
			<< JsonString(reason ? reason : "unknown") << ",\"origin\":";
		WriteVector(out, diagnostic.PrecommitOrigin);
		out << ",\"predicted_unsupported_endpoint\":";
		WriteVector(out, diagnostic.PredictedUnsupportedEndpoint);
		out << ",\"actual_unsupported_endpoint\":";
		WriteVector(out, diagnostic.ActualUnsupportedEndpoint);
		out << ",\"semantic_target\":" << JsonString(diagnostic.SemanticTarget)
			<< ",\"semantic_destination\":";
		WriteVector(out, diagnostic.SemanticDestination);
		out << ",\"start_support\":";
		WriteProbe(out, diagnostic.StartSupport);
		out << ",\"step_up\":";
		WriteProbe(out, diagnostic.StepUp);
		out << ",\"forward\":";
		WriteProbe(out, diagnostic.Forward);
		out << ",\"actual_step_down\":";
		WriteProbe(out, diagnostic.ActualStepDown);
		out << ",\"support_probe\":";
		WriteProbe(out, diagnostic.SupportProbe);
		out << ",\"fall_forecast\":{\"attempted\":"
			<< (diagnostic.FallForecastAttempted ? "true" : "false")
			<< ",\"origin\":";
		WriteVector(out, diagnostic.FallForecastOrigin);
		out << ",\"velocity\":";
		WriteVector(out, diagnostic.FallForecastVelocity);
		out << ",\"acceleration\":";
		WriteVector(out, diagnostic.FallForecastAcceleration);
		out << ",\"gravity_known\":"
			<< (diagnostic.FallForecastGravityKnown ? "true" : "false")
			<< ",\"gravity\":";
		WriteVector(out, diagnostic.FallForecastGravity);
		out << ",\"complete\":"
			<< (diagnostic.FallForecast.Complete ? "true" : "false")
			<< ",\"total_drop\":" << Fixed(diagnostic.FallForecast.TotalDrop, 6)
			<< ",\"continuation_count\":" << diagnostic.FallForecast.ContinuationCount
			<< ",\"landing_collision\":"
			<< JsonString(CollisionName(diagnostic.FallForecast.Landing.Collision))
			<< ",\"landing_normal\":";
		WriteVector(out, diagnostic.FallForecast.Landing.Normal);
		out << ",\"landing_zone\":"
			<< JsonString(ZoneName(diagnostic.FallForecast.Landing.Zone))
			<< ",\"pain_damage_per_sec_known\":"
			<< (diagnostic.FallForecast.PainDamagePerSecKnown ? "true" : "false")
			<< ",\"pain_damage_per_sec\":";
		if (diagnostic.FallForecast.PainDamagePerSecKnown
			&& std::isfinite(diagnostic.FallForecast.PainDamagePerSec))
		{
			out << Fixed(diagnostic.FallForecast.PainDamagePerSec, 6);
		}
		else
		{
			out << "null";
		}
		out
			<< ",\"hit_fractions\":[";
		for (size_t index = 0; index < diagnostic.FallHitCount; index++)
		{
			if (index) out << ',';
			out << Fixed(diagnostic.FallHitFractions[index], 9);
		}
		out << "]}}";
	}

	void WriteFallingParityRealizedRecord(std::ostringstream& out,
		const PawnMovement::FallingParityRealizedRecord& record)
	{
		out << "{\"source_pawn_actor\":"
			<< JsonString(record.Correlation.SourcePawnActor)
			<< ",\"life_generation\":\"" << record.Correlation.LifeGeneration
			<< "\",\"invocation_token\":\"" << record.Correlation.InvocationToken
			<< "\",\"walking_iteration\":" << record.Correlation.WalkingIteration
			<< ",\"step_ordinal\":\"" << record.StepOrdinal
			<< "\",\"outcome\":" << JsonString(
				PawnMovement::FallingParityRealizedOutcomeName(record.Outcome))
			<< ",\"elapsed\":" << Fixed(record.Elapsed, 9)
			<< ",\"collision\":" << JsonString(CollisionName(record.Collision))
			<< ",\"hit_fraction\":" << Fixed(record.HitFraction, 9)
			<< ",\"hit_normal\":";
		WriteVector(out, record.HitNormal);
		out << ",\"velocity_error\":" << Fixed(record.VelocityError, 9)
			<< ",\"requested_delta_error\":" << Fixed(record.RequestedDeltaError, 9)
			<< ",\"endpoint_error\":" << Fixed(record.EndpointError, 9)
			<< ",\"callback_barrier_mask\":\"" << record.CallbackBarrierMask
			<< "\"}";
	}

	const char* PositiveDpsVetoOutcomeName(
		PawnMovement::WalkingStepPreflightPositiveDpsVetoOutcome outcome)
	{
		switch (outcome)
		{
		case PawnMovement::WalkingStepPreflightPositiveDpsVetoOutcome::LegacyPainLedgeSuperseded:
			return "legacy_pain_ledge_superseded";
		case PawnMovement::WalkingStepPreflightPositiveDpsVetoOutcome::RollbackTestRejected:
			return "rollback_test_rejected";
		case PawnMovement::WalkingStepPreflightPositiveDpsVetoOutcome::RollbackActualRejected:
			return "rollback_actual_rejected";
		case PawnMovement::WalkingStepPreflightPositiveDpsVetoOutcome::Applied:
			return "applied";
		}
		return "unknown";
	}

	void WritePositiveDpsVetoAction(std::ostringstream& out,
		const PawnMovement::WalkingStepPreflightPositiveDpsVetoActionRecord& action)
	{
		out << "{\"source_pawn_actor\":" << JsonString(action.SourcePawnActor)
			<< ",\"sequence\":\"" << action.Sequence
			<< "\",\"life_generation\":\"" << action.LifeGeneration
			<< "\",\"invocation_token\":\"" << action.InvocationToken
			<< "\",\"walking_iteration\":" << action.WalkingIteration
			<< ",\"outcome\":" << JsonString(PositiveDpsVetoOutcomeName(action.Outcome))
			<< ",\"legacy_pain_ledge_superseded\":"
			<< (action.LegacyPainLedgeSuperseded ? "true" : "false")
			<< ",\"rollback_delta\":";
		WriteVector(out, action.RollbackDelta);
		out << ",\"rollback_test_attempted\":"
			<< (action.RollbackTestAttempted ? "true" : "false")
			<< ",\"rollback_test_fraction\":" << Fixed(action.RollbackTestFraction, 9)
			<< ",\"rollback_actual_attempted\":"
			<< (action.RollbackActualAttempted ? "true" : "false")
			<< ",\"rollback_actual_fraction\":" << Fixed(action.RollbackActualFraction, 9)
			<< ",\"forced_replan\":" << (action.ForcedReplan ? "true" : "false")
			<< '}';
	}

	void WriteFallingHazardZone(std::ostringstream& out,
		const PawnMovement::FallingHazardZoneId& zone)
	{
		out << "{\"known\":" << (zone.Known ? "true" : "false")
			<< ",\"zone_actor_id\":" << (zone.Known ? zone.ZoneActorId : 0)
			<< ",\"zone_number\":" << (zone.Known ? zone.ZoneNumber : 0)
			<< '}';
	}

	void WriteFallingHazardStartSummary(std::ostringstream& out,
		const PawnMovement::FallingHazardGenerationState& generation)
	{
		out << ",\"source\":" << JsonString(
			PawnMovement::FallingHazardForecastSourceName(generation.Source))
			<< ",\"forecast\":" << JsonString(
				PawnMovement::FallingHazardForecastName(generation.Forecast))
			<< ",\"starting_physics_zone\":";
		WriteFallingHazardZone(out, generation.StartingPhysicsZone);
		out << ",\"expected_harmful_foot_zone\":";
		WriteFallingHazardZone(out, generation.ExpectedHarmfulFootZone);
		out << ",\"expected_harmful_physics_zone\":";
		WriteFallingHazardZone(out, generation.ExpectedHarmfulPhysicsZone);
		out << ",\"expected_harmful_water_entry\":"
			<< (generation.ExpectedHarmfulWaterEntry ? "true" : "false")
			<< ",\"swept_segment_budget\":"
			<< generation.SweptSegmentBudget
			<< ",\"elapsed_horizon\":"
			<< Fixed(generation.ElapsedHorizon, 9);
	}

	void WriteFallingHazardDiagnostic(std::ostringstream& out,
		const PawnMovement::FallingHazardDiagnosticRecord& diagnostic)
	{
		const auto& generation = diagnostic.Generation;
		out << "{\"source_pawn_actor\":" << JsonString(diagnostic.SourcePawnActor)
			<< ",\"sequence\":\"" << diagnostic.Sequence
			<< "\",\"life_id\":\"" << generation.Life.Value
			<< "\",\"fall_episode_id\":\"" << generation.FallEpisode.Value
			<< "\",\"generation_id\":\"" << generation.Generation.Value
			<< "\",\"kind\":" << JsonString(
				PawnMovement::FallingHazardDiagnosticKindName(diagnostic.Kind));
		if (diagnostic.Kind
			== PawnMovement::FallingHazardDiagnosticKind::GenerationCapacityExceeded)
		{
			out << ",\"attempted_source\":" << JsonString(
				PawnMovement::FallingHazardForecastSourceName(generation.Source)) << '}';
			return;
		}

		WriteFallingHazardStartSummary(out, generation);
		if (diagnostic.Kind == PawnMovement::FallingHazardDiagnosticKind::Start)
		{
			out << ",\"precharged_elapsed\":"
				<< Fixed(diagnostic.PrechargedElapsed, 9) << '}';
			return;
		}

		out << ",\"terminal\":" << JsonString(
			PawnMovement::FallingHazardTerminalName(generation.Terminal))
			<< ",\"correlation\":" << JsonString(
				PawnMovement::FallingHazardCorrelationName(diagnostic.Correlation))
			<< ",\"last_observed_physics_zone\":";
		WriteFallingHazardZone(out, generation.LastObservedPhysicsZone);
		out << ",\"observed_harmful_foot_zone\":";
		WriteFallingHazardZone(out, generation.ObservedHarmfulFootZone);
		out << ",\"observed_harmful_center_zone\":";
		WriteFallingHazardZone(out, generation.ObservedHarmfulCenterZone);
		out << ",\"swept_segment_count\":"
			<< generation.SweptSegmentCount
			<< ",\"observed_elapsed\":"
			<< Fixed(generation.ObservedElapsed, 9)
			<< ",\"has_positive_elapsed\":"
			<< (generation.HasPositiveElapsed ? "true" : "false")
			<< ",\"physics_zone_evidence_known\":"
			<< (generation.PhysicsZoneEvidenceKnown ? "true" : "false")
			<< ",\"harmful_foot_evidence_known\":"
			<< (generation.HarmfulFootEvidenceKnown ? "true" : "false")
			<< ",\"harmful_center_evidence_known\":"
			<< (generation.HarmfulCenterEvidenceKnown ? "true" : "false")
			<< ",\"water_evidence_known\":"
			<< (generation.WaterEvidenceKnown ? "true" : "false")
			<< ",\"entered_harmful_foot_zone\":"
			<< (generation.EnteredHarmfulFootZone ? "true" : "false")
			<< ",\"entered_harmful_center_zone\":"
			<< (generation.EnteredHarmfulCenterZone ? "true" : "false")
			<< ",\"expected_harmful_path_matched\":"
			<< (generation.ExpectedHarmfulPathMatched ? "true" : "false")
			<< ",\"causal_ambiguity\":"
			<< (generation.CausalAmbiguity ? "true" : "false")
			<< ",\"actual_trajectory_unknown\":"
			<< (generation.ActualTrajectoryUnknown ? "true" : "false")
			<< ",\"landing_collision\":" << JsonString(
				PawnMovement::FallingHazardCollisionName(generation.LandingCollision))
			<< '}';
	}

	void WriteBot(std::ostringstream& out, const BotBenchmarkBotState& bot)
	{
		out << "{\"identity\":" << JsonString(bot.Identity)
			<< ",\"actor\":" << JsonString(bot.Actor)
			<< ",\"player_name\":" << JsonString(bot.PlayerName)
			<< ",\"class\":" << JsonString(bot.ClassName)
			<< ",\"position\":{"
			<< "\"x\":" << Fixed(bot.PositionX, 6)
			<< ",\"y\":" << Fixed(bot.PositionY, 6)
			<< ",\"z\":" << Fixed(bot.PositionZ, 6) << "}"
			<< ",\"velocity\":{"
			<< "\"x\":" << Fixed(bot.VelocityX, 6)
			<< ",\"y\":" << Fixed(bot.VelocityY, 6)
			<< ",\"z\":" << Fixed(bot.VelocityZ, 6) << "}"
			<< ",\"physics_mode\":" << JsonString(bot.PhysicsMode)
			<< ",\"latent_action\":" << JsonString(bot.LatentAction)
			<< ",\"acceleration\":{"
			<< "\"x\":" << Fixed(bot.AccelerationX, 6)
			<< ",\"y\":" << Fixed(bot.AccelerationY, 6)
			<< ",\"z\":" << Fixed(bot.AccelerationZ, 6) << "}"
			<< ",\"destination\":{"
			<< "\"x\":" << Fixed(bot.DestinationX, 6)
			<< ",\"y\":" << Fixed(bot.DestinationY, 6)
			<< ",\"z\":" << Fixed(bot.DestinationZ, 6) << "}"
			<< ",\"move_timer\":" << Fixed(bot.MoveTimer, 6)
			<< ",\"move_target_identity\":" << JsonString(bot.MoveTargetIdentity)
			<< ",\"move_target_name\":" << JsonString(bot.MoveTargetName)
			<< ",\"health\":" << bot.Health
			<< ",\"score\":" << Fixed(bot.Score, 6)
			<< ",\"pri_deaths\":" << Fixed(bot.PriDeaths, 6)
			<< ",\"movement_intent\":" << (bot.MovementIntent ? "true" : "false")
			<< ",\"in_hazard_zone\":" << (bot.InHazardZone ? "true" : "false")
			<< ",\"kills_exact\":\"" << bot.KillsExact << "\""
			<< ",\"deaths_exact\":\"" << bot.DeathsExact << "\""
			<< ",\"suicides_exact\":\"" << bot.SuicidesExact << "\""
			<< ",\"environmental_deaths_exact\":\"" << bot.EnvironmentalDeathsExact << "\""
			<< ",\"hazard_exposed_deaths_proxy\":\"" << bot.HazardExposedDeathsProxy << "\""
			<< ",\"direct_self_kills\":\"" << bot.DirectSelfKills << "\""
			<< ",\"direct_enemy_kills\":\"" << bot.DirectEnemyKills << "\""
			<< ",\"unassisted_environmental_deaths\":\"" << bot.UnassistedEnvironmentalDeaths << "\""
			<< ",\"recent_enemy_contributed_environmental_deaths_proxy\":\"" << bot.RecentEnemyContributedEnvironmentalDeathsProxy << "\""
			<< ",\"ambiguous_deaths\":\"" << bot.AmbiguousDeaths << "\""
			<< ",\"recent_enemy_momentum_contributed_environmental_deaths_proxy\":\"" << bot.RecentEnemyMomentumContributedEnvironmentalDeathsProxy << "\""
			<< ",\"hit_wall_events_exact\":\"" << bot.HitWallEventsExact << "\""
			<< ",\"pain_ledge_vetoes_exact\":\"" << bot.PainLedgeVetoesExact << "\""
			<< ",\"pain_ledge_repeat_vetoes_exact\":\"" << bot.PainLedgeRepeatVetoesExact << "\""
			<< ",\"pain_ledge_recovery_attempts_exact\":\"" << bot.PainLedgeRecoveryAttemptsExact << "\""
			<< ",\"pain_ledge_recovery_escapes_exact\":\"" << bot.PainLedgeRecoveryEscapesExact << "\""
			<< ",\"wall_adjust_calls_exact\":\"" << bot.WallAdjustCallsExact << "\""
			<< ",\"wall_adjust_repeats_exact\":\"" << bot.WallAdjustRepeatsExact << "\""
			<< ",\"wall_adjust_recovery_attempts_exact\":\"" << bot.WallAdjustRecoveryAttemptsExact << "\""
			<< ",\"wall_adjust_recovery_successes_exact\":\"" << bot.WallAdjustRecoverySuccessesExact << "\""
			<< ",\"wall_adjust_forced_replans_exact\":\"" << bot.WallAdjustForcedReplansExact << "\""
			<< ",\"move_stall_detections_exact\":\"" << bot.MoveStallDetectionsExact << "\""
			<< ",\"move_stall_episode_resets_exact\":\"" << bot.MoveStallEpisodeResetsExact << "\""
			<< ",\"move_stall_forced_replans_exact\":\"" << bot.MoveStallForcedReplansExact << "\""
			<< ",\"move_stall_navigation_forced_replans_exact\":\"" << bot.MoveStallNavigationForcedReplansExact << "\""
			<< ",\"move_stall_targetless_move_to_timeouts_exact\":\"" << bot.MoveStallTargetlessMoveToTimeoutsExact << "\""
			<< ",\"move_stall_eligible_seconds\":" << Fixed(bot.MoveStallEligibleSeconds, 9)
			<< ",\"failed_navigation_avoidance_activations_exact\":\"" << bot.FailedNavigationAvoidanceActivationsExact << "\""
			<< ",\"failed_navigation_safeguard_suppressions_exact\":\"" << bot.FailedNavigationSafeguardSuppressionsExact << "\""
			<< ",\"failed_navigation_route_penalty_applications_exact\":\"" << bot.FailedNavigationRoutePenaltyApplicationsExact << "\""
			<< ",\"harmful_zone_escape_episodes_exact\":\"" << bot.HarmfulZoneEscapeEpisodesExact << "\""
			<< ",\"harmful_zone_escape_center_entries_exact\":\"" << bot.HarmfulZoneEscapeCenterEntriesExact << "\""
			<< ",\"harmful_zone_escape_foot_entries_exact\":\"" << bot.HarmfulZoneEscapeFootEntriesExact << "\""
			<< ",\"harmful_zone_escape_recovery_attempts_exact\":\"" << bot.HarmfulZoneEscapeRecoveryAttemptsExact << "\""
			<< ",\"harmful_zone_escape_successful_escapes_exact\":\"" << bot.HarmfulZoneEscapeSuccessfulEscapesExact << "\""
			<< ",\"harmful_zone_escape_forced_replans_exact\":\"" << bot.HarmfulZoneEscapeForcedReplansExact << "\""
			<< ",\"harmful_zone_escape_no_safe_candidates_exact\":\"" << bot.HarmfulZoneEscapeNoSafeCandidatesExact << "\""
			<< ",\"hazard_swim_egress_episodes_exact\":\"" << bot.HazardSwimEgressEpisodesExact << "\""
			<< ",\"hazard_swim_egress_eligible_exact\":\"" << bot.HazardSwimEgressEligibleExact << "\""
			<< ",\"hazard_swim_egress_authorized_exact\":\"" << bot.HazardSwimEgressAuthorizedExact << "\""
			<< ",\"hazard_swim_egress_debounced_exact\":\"" << bot.HazardSwimEgressDebouncedExact << "\""
			<< ",\"hazard_swim_egress_no_anchor_rejected_exact\":\"" << bot.HazardSwimEgressNoAnchorRejectedExact << "\""
			<< ",\"hazard_swim_egress_exited_exact\":\"" << bot.HazardSwimEgressExitedExact << "\""
			<< ",\"hazard_swim_egress_died_before_exit_exact\":\"" << bot.HazardSwimEgressDeathsBeforeExitExact << "\""
			<< ",\"hazard_swim_egress_forced_replans_exact\":\"" << bot.HazardSwimEgressForcedReplansExact << "\""
			<< ",\"hazard_swim_egress_falling_pre_move_anchor_captures_exact\":\"" << bot.HazardSwimEgressFallingPreMoveAnchorCapturesExact << "\""
			<< ",\"hazard_swim_egress_falling_pre_move_anchor_uses_exact\":\"" << bot.HazardSwimEgressFallingPreMoveAnchorUsesExact << "\""
			<< ",\"hazard_swim_egress_anchor_known\":" << (bot.HazardSwimEgressAnchorKnown ? "true" : "false")
			<< ",\"hazard_swim_egress_anchor_source\":" << JsonString(bot.HazardSwimEgressAnchorSource)
			<< ",\"falling_seam_detections_exact\":\"" << bot.FallingSeamDetectionsExact << "\""
			<< ",\"horizontal_corner_candidate_probes_exact\":\"" << bot.HorizontalCornerCandidateProbesExact << "\""
			<< ",\"horizontal_corner_authorized_escapes_exact\":\"" << bot.HorizontalCornerAuthorizedEscapesExact << "\""
			<< ",\"horizontal_corner_target_progress_rejects_exact\":\"" << bot.HorizontalCornerTargetProgressRejectsExact << "\""
			<< ",\"horizontal_corner_unknown_or_unsafe_support_exact\":\"" << bot.HorizontalCornerUnknownOrUnsafeSupportExact << "\""
			<< ",\"falling_seam_episodes_exact\":\"" << bot.FallingSeamEpisodesExact << "\""
			<< ",\"falling_seam_invalid_geometry_rejects_exact\":\"" << bot.FallingSeamInvalidGeometryRejectsExact << "\""
			<< ",\"falling_seam_authorizable_episodes_exact\":\"" << bot.FallingSeamAuthorizableEpisodesExact << "\""
			<< ",\"horizontal_corner_authorized_candidates_exact\":\"" << bot.HorizontalCornerAuthorizedCandidatesExact << "\""
			<< ",\"horizontal_corner_blocked_sweep_candidates_exact\":\"" << bot.HorizontalCornerBlockedSweepCandidatesExact << "\""
			<< ",\"horizontal_corner_no_static_walkable_support_candidates_exact\":\"" << bot.HorizontalCornerNoStaticWalkableSupportCandidatesExact << "\""
			<< ",\"horizontal_corner_pain_support_candidates_exact\":\"" << bot.HorizontalCornerPainSupportCandidatesExact << "\""
			<< ",\"horizontal_corner_no_active_movement_intent_or_target_candidates_exact\":\"" << bot.HorizontalCornerNoActiveMovementIntentOrTargetCandidatesExact << "\""
			<< ",\"horizontal_corner_true_target_regression_candidates_exact\":\"" << bot.HorizontalCornerTrueTargetRegressionCandidatesExact << "\""
			<< ",\"horizontal_corner_unknown_evidence_candidates_exact\":\"" << bot.HorizontalCornerUnknownEvidenceCandidatesExact << "\""
			<< ",\"walking_step_preflight_observations_exact\":\"" << bot.WalkingStepPreflightObservationsExact << "\""
			<< ",\"walking_step_preflight_unsupported_endpoints_exact\":\"" << bot.WalkingStepPreflightUnsupportedEndpointsExact << "\""
			<< ",\"walking_step_preflight_no_decisions_exact\":\"" << bot.WalkingStepPreflightNoDecisionsExact << "\""
			<< ",\"walking_step_preflight_provisional_authorizations_exact\":\"" << bot.WalkingStepPreflightProvisionalAuthorizationsExact << "\""
			<< ",\"walking_step_preflight_post_mayfall_confirmed_authorizations_exact\":\"" << bot.WalkingStepPreflightAuthorizationsExact << "\""
			<< ",\"walking_step_preflight_authorizable_episodes_exact\":\"" << bot.WalkingStepPreflightAuthorizableEpisodesExact << "\"";
		out << ",\"walking_step_preflight_positive_dps_veto_eligible_exact\":\""
			<< bot.WalkingStepPreflightPositiveDpsVetoEligibleExact << "\""
			<< ",\"walking_step_preflight_positive_dps_veto_applied_exact\":\""
			<< bot.WalkingStepPreflightPositiveDpsVetoAppliedExact << "\""
			<< ",\"walking_step_preflight_positive_dps_veto_debounced_exact\":\""
			<< bot.WalkingStepPreflightPositiveDpsVetoDebouncedExact << "\""
			<< ",\"walking_step_preflight_positive_dps_veto_forced_replans_exact\":\""
			<< bot.WalkingStepPreflightPositiveDpsVetoForcedReplansExact << "\""
			<< ",\"walking_step_preflight_positive_dps_veto_rollback_rejected_exact\":\""
			<< bot.WalkingStepPreflightPositiveDpsVetoRollbackRejectedExact << "\"";
		for (size_t reasonIndex = 0;
			reasonIndex < bot.WalkingStepPreflightReasonsExact.size(); reasonIndex++)
		{
			const char* metric = PawnMovement::WalkingStepPreflightReasonMetricName(
				static_cast<PawnMovement::WalkingStepPreflightReason>(reasonIndex));
			if (metric)
				out << ",\"" << metric << "\":\""
					<< bot.WalkingStepPreflightReasonsExact[reasonIndex] << "\"";
		}
		out << ",\"walking_step_preflight_diagnostic_overflows_exact\":\""
			<< bot.WalkingStepPreflightDiagnosticOverflowsExact << "\""
			<< ",\"walking_step_preflight_diagnostics\":[";
		for (size_t index = 0; index < bot.WalkingStepPreflightDiagnostics.size(); index++)
		{
			if (index) out << ',';
			WritePreflightDiagnostic(out, bot.WalkingStepPreflightDiagnostics[index]);
		}
		out << ']' << ",\"walking_step_preflight_positive_dps_veto_action_overflows_exact\":\""
			<< bot.WalkingStepPreflightPositiveDpsVetoActionOverflowsExact << "\""
			<< ",\"walking_step_preflight_positive_dps_veto_actions\":[";
		for (size_t index = 0; index < bot.WalkingStepPreflightPositiveDpsVetoActions.size(); index++)
		{
			if (index) out << ',';
			WritePositiveDpsVetoAction(out, bot.WalkingStepPreflightPositiveDpsVetoActions[index]);
		}
		out << ']';
		out << ",\"falling_parity_realized_episodes_exact\":\""
			<< bot.FallingParityRealizedEpisodesExact << "\""
			<< ",\"falling_parity_realized_steps_exact\":\""
			<< bot.FallingParityRealizedStepsExact << "\""
			<< ",\"falling_parity_realized_matched_steps_exact\":\""
			<< bot.FallingParityRealizedMatchedStepsExact << "\""
			<< ",\"falling_parity_realized_matched_landing_steps_exact\":\""
			<< bot.FallingParityRealizedMatchedLandingStepsExact << "\""
			<< ",\"falling_parity_realized_mismatches_exact\":\""
			<< bot.FallingParityRealizedMismatchesExact << "\""
			<< ",\"falling_parity_realized_unknowns_exact\":\""
			<< bot.FallingParityRealizedUnknownsExact << "\""
			<< ",\"falling_parity_realized_callback_barriers_exact\":\""
			<< bot.FallingParityRealizedCallbackBarriersExact << "\""
			<< ",\"falling_parity_realized_pain_entries_exact\":\""
			<< bot.FallingParityRealizedPainEntriesExact << "\""
			<< ",\"falling_parity_realized_deaths_exact\":\""
			<< bot.FallingParityRealizedDeathsExact << "\""
			<< ",\"falling_parity_realized_landings_exact\":\""
			<< bot.FallingParityRealizedLandingsExact << "\""
			<< ",\"falling_parity_realized_continuity_losses_exact\":\""
			<< bot.FallingParityRealizedContinuityLossesExact << "\""
			<< ",\"falling_parity_realized_record_overflows_exact\":\""
			<< bot.FallingParityRealizedRecordOverflowsExact << "\""
			<< ",\"falling_parity_realized_records\":[";
		for (size_t index = 0; index < bot.FallingParityRealizedRecords.size(); index++)
		{
			if (index) out << ',';
			WriteFallingParityRealizedRecord(out,
				bot.FallingParityRealizedRecords[index]);
		}
		out << ']';
		out << ",\"vertical_pain_column_episodes_started_exact\":\""
			<< bot.VerticalPainColumnEpisodesStartedExact << "\""
			<< ",\"vertical_pain_column_episodes_completed_exact\":\""
			<< bot.VerticalPainColumnEpisodesCompletedExact << "\""
			<< ",\"vertical_pain_column_true_positive_outcomes_exact\":\""
			<< bot.VerticalPainColumnTruePositiveOutcomesExact << "\""
			<< ",\"vertical_pain_column_false_positive_outcomes_exact\":\""
			<< bot.VerticalPainColumnFalsePositiveOutcomesExact << "\""
			<< ",\"vertical_pain_column_false_negative_outcomes_exact\":\""
			<< bot.VerticalPainColumnFalseNegativeOutcomesExact << "\""
			<< ",\"vertical_pain_column_true_negative_outcomes_exact\":\""
			<< bot.VerticalPainColumnTrueNegativeOutcomesExact << "\""
			<< ",\"vertical_pain_column_ambiguous_outcomes_exact\":\""
			<< bot.VerticalPainColumnAmbiguousOutcomesExact << "\""
			<< ",\"vertical_pain_column_unknown_outcomes_exact\":\""
			<< bot.VerticalPainColumnUnknownOutcomesExact << "\""
			<< ",\"vertical_pain_column_diagnostic_overflows_exact\":\""
			<< bot.VerticalPainColumnDiagnosticOverflowsExact << "\""
			<< ",\"vertical_pain_column_generation_capacity_exhaustions_exact\":\""
			<< bot.VerticalPainColumnGenerationCapacityExhaustionsExact << "\""
			<< ",\"vertical_pain_column_diagnostics\":[";
		for (size_t index = 0; index < bot.VerticalPainColumnDiagnostics.size(); index++)
		{
			if (index) out << ',';
			WriteFallingHazardDiagnostic(out,
				bot.VerticalPainColumnDiagnostics[index]);
		}
		out << ']';
		out << ",\"state\":" << JsonString(bot.State) << "}";
	}

	void WriteRequestedRoster(std::ostringstream& out, const BotBenchmarkRoster& roster)
	{
		out << "  \"requested_roster\": [\n";
		const auto& participants = roster.GetParticipants();
		for (size_t index = 0; index < participants.size(); index++)
		{
			const auto& participant = participants[index];
			out << "    {\"roster_index\": " << participant.RosterIndex
				<< ", \"requested_name\": " << JsonString(participant.RequestedName)
				<< ", \"external_skill\": " << participant.ExternalSkill
				<< ", \"identity_fragment\": " << JsonString(participant.CanonicalIdentityFragment) << "}";
			out << (index + 1 == participants.size() ? "\n" : ",\n");
		}
		out << "  ]";
	}
}

uint64_t BotBenchmarkTelemetryProtocol::EventCap(uint64_t maxTicks)
{
	if (maxTicks > UINT64_MAX - 2)
		throw std::overflow_error("bot benchmark telemetry event cap overflow");
	return maxTicks + 2;
}

std::string BotBenchmarkTelemetryProtocol::ConfigIdentity(const BotBenchmarkRunConfig& config)
{
	std::ostringstream canonical;
	canonical.imbue(std::locale::classic());
	canonical << "url=" << config.GetURL() << '\n'
		<< "seed=" << config.GetSeed() << '\n'
		<< "max_ticks=" << config.GetMaxTicks() << '\n'
		<< "fixed_delta=" << Fixed(config.GetFixedDelta(), 9) << '\n'
		<< "difficulty=" << config.GetDifficulty() << '\n'
		<< "bot_count=" << config.GetRoster().GetCount() << '\n'
		<< "harmful_zone_escape_enabled="
		<< (config.IsHarmfulZoneEscapeEnabled() ? "1" : "0") << '\n'
		<< "walking_preflight_positive_dps_veto_enabled="
		<< (config.IsWalkingPreflightPositiveDpsVetoEnabled() ? "1" : "0") << '\n'
		<< "hazard_swim_egress_enabled="
		<< (config.IsHazardSwimEgressEnabled() ? "1" : "0") << '\n'
		<< "hazard_swim_egress_live_enabled="
		<< (config.IsHazardSwimEgressLiveEnabled() ? "1" : "0") << '\n';
	for (const auto& participant : config.GetRoster().GetParticipants())
		canonical << "roster=" << participant.CanonicalIdentityFragment << '\n';
	uint64_t digest = 1469598103934665603ULL;
	Hash(digest, canonical.str());
	return "fnv1a64:" + Hex64(digest);
}

std::string BotBenchmarkTelemetryProtocol::ManifestJson(const BotBenchmarkRunConfig& config)
{
	std::ostringstream out;
	out.imbue(std::locale::classic());
	out << "{\n"
		<< "  \"schema\": \"surreal-bot-benchmark-manifest-v2\",\n"
		<< "  \"driver\": \"bot-benchmark\",\n"
		<< "  \"config_id\": " << JsonString(ConfigIdentity(config)) << ",\n"
		<< "  \"url\": " << JsonString(config.GetURL()) << ",\n"
		<< "  \"output_directory\": " << JsonString(config.GetOutputDirectory()) << ",\n"
		<< "  \"seed\": \"" << config.GetSeed() << "\",\n"
		<< "  \"max_ticks\": \"" << config.GetMaxTicks() << "\",\n"
		<< "  \"fixed_delta\": " << Fixed(config.GetFixedDelta(), 9) << ",\n"
		<< "  \"difficulty\": " << config.GetDifficulty() << ",\n"
		<< "  \"bot_count\": " << config.GetRoster().GetCount() << ",\n";
	WriteRequestedRoster(out, config.GetRoster());
	out << ",\n"
		<< "  \"telemetry_event_cap\": \"" << EventCap(config.GetMaxTicks()) << "\",\n"
		<< "  \"harmful_zone_escape_enabled\": "
		<< (config.IsHarmfulZoneEscapeEnabled() ? "true" : "false") << ",\n"
		<< "  \"walking_preflight_positive_dps_veto_enabled\": "
		<< (config.IsWalkingPreflightPositiveDpsVetoEnabled() ? "true" : "false") << ",\n"
		<< "  \"hazard_swim_egress_enabled\": "
		<< (config.IsHazardSwimEgressEnabled() ? "true" : "false") << ",\n"
		<< "  \"hazard_swim_egress_live_enabled\": "
		<< (config.IsHazardSwimEgressLiveEnabled() ? "true" : "false") << ",\n"
		<< "  \"death_attribution_recent_window_seconds\": 2.000000000,\n"
		<< "  \"suicides_exact_semantics\": \"legacy_scoreboard_self_or_nonplayer_killer\"\n"
		<< "}\n";
	return out.str();
}

std::string BotBenchmarkTelemetryProtocol::EventJson(const std::string& configIdentity, BotBenchmarkTelemetryEvent event)
{
	std::sort(event.Bots.begin(), event.Bots.end(), [](const BotBenchmarkBotState& a, const BotBenchmarkBotState& b)
	{
		if (a.Identity != b.Identity)
			return a.Identity < b.Identity;
		return a.Actor < b.Actor;
	});

	std::ostringstream out;
	out.imbue(std::locale::classic());
	out << "{\"schema\":\"surreal-bot-benchmark-telemetry-v2\""
		<< ",\"seq\":\"" << event.Sequence << "\""
		<< ",\"config_id\":" << JsonString(configIdentity)
		<< ",\"tick\":\"" << event.Tick << "\""
		<< ",\"simulated_seconds\":" << Fixed(event.SimulatedSeconds, 9)
		<< ",\"type\":" << JsonString(event.Type)
		<< ",\"map\":" << JsonString(event.Map)
		<< ",\"status\":" << JsonString(event.Status)
		<< ",\"failure_reason\":" << JsonString(event.FailureReason)
		<< ",\"bots\":[";
	for (size_t index = 0; index < event.Bots.size(); index++)
	{
		if (index != 0)
			out << ',';
		WriteBot(out, event.Bots[index]);
	}
	out << "]}\n";
	return out.str();
}
