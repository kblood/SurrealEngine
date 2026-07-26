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
			<< "\",\"movement_command_token\":\"" << diagnostic.MovementCommandToken
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

	void WriteOptionalFinite(std::ostringstream& out, float value, int precision)
	{
		if (std::isfinite(value))
			out << Fixed(value, precision);
		else
			out << "null";
	}

	void WriteOptionalVector(std::ostringstream& out, const vec3& value)
	{
		if (!std::isfinite(value.x) || !std::isfinite(value.y)
			|| !std::isfinite(value.z))
		{
			out << "null";
			return;
		}
		WriteVector(out, value);
	}

	void WriteWalkingHitWallDispatchDiagnostic(std::ostringstream& out,
		const PawnMovement::WalkingHitWallDispatchDiagnosticRecord& diagnostic)
	{
		out << "{\"source_pawn_actor\":" << JsonString(diagnostic.SourcePawnActor)
			<< ",\"sequence\":\"" << diagnostic.Sequence
			<< "\",\"contact_phase\":" << JsonString(
				PawnMovement::WalkingHitWallContactPhaseName(diagnostic.ContactPhase))
			<< ",\"hit_normal\":";
		WriteOptionalVector(out, diagnostic.HitNormal);
		out << ",\"velocity\":";
		WriteOptionalVector(out, diagnostic.Velocity);
		out << ",\"min_hit_wall\":";
		WriteOptionalFinite(out, diagnostic.MinHitWall, 6);
		out << ",\"normal_velocity_dot\":";
		WriteOptionalFinite(out, diagnostic.Decision.NormalVelocityDot, 6);
		out << ",\"valid\":" << (diagnostic.Decision.Valid ? "true" : "false")
			<< ",\"legacy_vertical_wall_band\":"
			<< (diagnostic.Decision.LegacyVerticalWallBand ? "true" : "false")
			<< ",\"min_hit_wall_dispatch\":"
			<< (diagnostic.Decision.MinHitWallDispatch ? "true" : "false")
			<< ",\"blocker\":" << JsonString(
				PawnMovement::WalkingHitWallBlockerKindName(diagnostic.Blocker))
			<< ",\"callback_dispatched\":"
			<< (diagnostic.CallbackDispatched ? "true" : "false")
			<< ",\"physics_changed_by_callback\":"
			<< (diagnostic.PhysicsChangedByCallback ? "true" : "false")
			<< ",\"pawn_deleted_by_callback\":"
			<< (diagnostic.PawnDeletedByCallback ? "true" : "false") << '}';
	}

	const char* MoveStallRecoveryEpisodeOutcomeName(
		PawnMovement::MoveStallRecoveryEpisodeOutcome outcome)
	{
		using PawnMovement::MoveStallRecoveryEpisodeOutcome;
		switch (outcome)
		{
		case MoveStallRecoveryEpisodeOutcome::ClearedWithin2Seconds:
			return "cleared_within_2_seconds";
		case MoveStallRecoveryEpisodeOutcome::ClearedAfter2SecondsWithin5Seconds:
			return "cleared_after_2_seconds_within_5_seconds";
		case MoveStallRecoveryEpisodeOutcome::ReplannedWithin5Seconds:
			return "replanned_within_5_seconds";
		case MoveStallRecoveryEpisodeOutcome::Missed5SecondDeadline:
			return "missed_5_second_deadline";
		case MoveStallRecoveryEpisodeOutcome::ExcludedIntentionalStop:
			return "excluded_intentional_stop";
		case MoveStallRecoveryEpisodeOutcome::CensoredLifeBoundary:
			return "censored_life_boundary";
		case MoveStallRecoveryEpisodeOutcome::CensoredRunEnd:
			return "censored_run_end";
		case MoveStallRecoveryEpisodeOutcome::Unknown:
			return "unknown";
		case MoveStallRecoveryEpisodeOutcome::None:
			return "none";
		}
		return "unknown";
	}

	void WriteMoveStallRecoveryEpisode(std::ostringstream& out,
		const PawnMoveStallRecoveryEpisodeRecord& record)
	{
		out << "{\"source_pawn_actor\":" << JsonString(record.SourcePawnActor)
			<< ",\"sequence\":\"" << record.Sequence
			<< "\",\"life_id\":\"" << record.LifeId
			<< "\",\"episode_id\":\"" << record.EpisodeId
			<< "\",\"seconds_since_detection\":"
			<< Fixed(record.SecondsSinceDetection, 9)
			<< ",\"outcome\":"
			<< JsonString(MoveStallRecoveryEpisodeOutcomeName(record.Outcome)) << '}';
	}

	const char* MoveStallLatentModeName(PawnMovement::MoveStallLatentMode mode)
	{
		using PawnMovement::MoveStallLatentMode;
		switch (mode)
		{
		case MoveStallLatentMode::MoveTo: return "move_to";
		case MoveStallLatentMode::MoveToward: return "move_toward";
		case MoveStallLatentMode::StrafeTo: return "strafe_to";
		case MoveStallLatentMode::StrafeFacing: return "strafe_facing";
		case MoveStallLatentMode::Other: return "other";
		}
		return "other";
	}

	const char* MoveStallRecoveryDecisionName(PawnMovement::MoveStallRecoveryDecision decision)
	{
		using PawnMovement::MoveStallRecoveryDecision;
		switch (decision)
		{
		case MoveStallRecoveryDecision::None: return "none";
		case MoveStallRecoveryDecision::NavigationReplan: return "navigation_replan";
		case MoveStallRecoveryDecision::TargetlessTimeout: return "targetless_timeout";
		case MoveStallRecoveryDecision::DirectActorMoveTowardTimeout:
			return "direct_actor_move_toward_timeout";
		}
		return "none";
	}

	void WriteMoveStallRecoveryDecision(std::ostringstream& out,
		const PawnMoveStallRecoveryDecisionRecord& record)
	{
		out << "{\"source_pawn_actor\":" << JsonString(record.SourcePawnActor)
			<< ",\"sequence\":\"" << record.Sequence
			<< "\",\"life_id\":\"" << record.LifeId
			<< "\",\"episode_id\":\"" << record.EpisodeId
			<< "\",\"native_tick\":\"" << record.NativeTick
			<< "\",\"latent_mode\":" << JsonString(MoveStallLatentModeName(record.LatentMode))
			<< ",\"decision\":" << JsonString(MoveStallRecoveryDecisionName(record.Decision))
			<< ",\"no_progress_seconds\":" << Fixed(record.NoProgressSeconds, 9)
			<< ",\"no_progress_displacement\":" << Fixed(record.NoProgressDisplacement, 9)
			<< ",\"progress_radius\":" << Fixed(record.ProgressRadius, 9)
			<< ",\"move_target_known\":" << (record.MoveTargetKnown ? "true" : "false")
			<< ",\"move_target_live\":" << (record.MoveTargetLive ? "true" : "false")
			<< ",\"move_target_actor_index\":" << record.MoveTargetActorIndex
			<< ",\"move_target_name\":" << JsonString(record.MoveTargetName)
			<< ",\"move_target_class\":" << JsonString(record.MoveTargetClass)
			<< ",\"move_target_is_inventory\":"
			<< (record.MoveTargetIsInventory ? "true" : "false")
			<< ",\"marker_known\":" << (record.MarkerKnown ? "true" : "false")
			<< ",\"marker_live\":" << (record.MarkerLive ? "true" : "false")
			<< ",\"marker_actor_index\":" << record.MarkerActorIndex
			<< ",\"marker_name\":" << JsonString(record.MarkerName)
			<< ",\"marker_class\":" << JsonString(record.MarkerClass)
			<< ",\"move_timer\":" << Fixed(record.MoveTimer, 9) << '}';
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
		out << ",\"aligned_command_provenance\":" << JsonString(
			PawnMovement::FallingHazardAlignedCommandProvenanceName(
				diagnostic.AlignedCommandProvenance));
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

	void WriteHazardWaterEgressDiagnostic(std::ostringstream& out,
		const PawnMovement::HazardWaterEgressDiagnosticRecord& diagnostic)
	{
		const auto& entry = diagnostic.Entry;
		out << "{\"source_pawn_actor\":" << JsonString(diagnostic.SourcePawnActor)
			<< ",\"sequence\":\"" << diagnostic.Sequence
			<< "\",\"life_id\":\"" << entry.LifeId
			<< "\",\"episode_id\":\"" << entry.EpisodeId
			<< "\",\"transition_source\":" << JsonString(
				PawnMovement::HazardWaterEgressTransitionSourceName(entry.TransitionSource))
			<< ",\"anchor_known\":" << (entry.AnchorKnown ? "true" : "false")
			<< ",\"anchor\":";
		WriteVector(out, entry.Anchor);
		out << ",\"entry_location\":";
		WriteVector(out, entry.EntryLocation);
		out << ",\"damage_per_second\":" << Fixed(entry.DamagePerSecond, 6)
			<< ",\"entry_move_target_name\":" << JsonString(entry.MoveTargetName)
			<< ",\"entry_move_target_location_known\":"
			<< (entry.MoveTargetLocationKnown ? "true" : "false")
			<< ",\"entry_move_target_location\":";
		WriteVector(out, entry.MoveTargetLocation);
		out << ",\"entry_destination\":";
		WriteVector(out, entry.Destination);
		out << ",\"falling_launch_snapshot_known\":"
			<< (entry.ExternalImpulseNavigationCommitKnown ? "true" : "false")
			<< ",\"falling_launch_life_id\":\""
			<< entry.ExternalImpulseNavigationCommitLifeId
			<< "\",\"falling_launch_movement_command_active\":"
			<< (entry.ExternalImpulseMovementCommandActive ? "true" : "false")
			<< ",\"falling_launch_movement_command_token\":\""
			<< entry.ExternalImpulseMovementCommandToken
			<< "\",\"falling_launch_movement_command_kind\":"
			<< JsonString(entry.ExternalImpulseMovementCommandKind)
			<< ",\"falling_launch_movement_command_target_name\":"
			<< JsonString(entry.ExternalImpulseMovementCommandTargetName)
			<< ",\"falling_launch_movement_command_destination\":";
		WriteVector(out, entry.ExternalImpulseMovementCommandDestination);
		out << ",\"falling_launch_move_target_name\":"
			<< JsonString(entry.ExternalImpulseMoveTargetName)
			<< ",\"falling_launch_move_target_navigation\":"
			<< (entry.ExternalImpulseMoveTargetNavigation ? "true" : "false")
			<< ",\"falling_launch_route_head_known\":"
			<< (entry.ExternalImpulseRouteHeadKnown ? "true" : "false")
			<< ",\"falling_launch_route_head_name\":"
			<< JsonString(entry.ExternalImpulseRouteHeadName)
			<< ",\"falling_launch_location\":";
		WriteVector(out, entry.ExternalImpulseCommitLocation);
		out << ",\"falling_launch_velocity\":";
		WriteVector(out, entry.ExternalImpulseCommitVelocity);
		out << ",\"falling_launch_forecast_known\":"
			<< (entry.ExternalImpulseLaunchForecastKnown ? "true" : "false")
			<< ",\"falling_launch_forecast_harmful\":"
			<< (entry.ExternalImpulseLaunchForecastHarmful ? "true" : "false");
		const auto& certificate = diagnostic.StaticWalkCertificate;
		out << ",\"static_walk_certificate_result\":"
			<< JsonString(certificate.Result)
			<< ",\"static_walk_first_hop_known\":"
			<< (certificate.FirstHopKnown ? "true" : "false")
			<< ",\"static_walk_first_hop_name\":"
			<< JsonString(certificate.FirstHopName)
			<< ",\"static_walk_first_hop_location_known\":"
			<< (certificate.FirstHopLocationKnown ? "true" : "false")
			<< ",\"static_walk_first_hop_location\":";
		WriteVector(out, certificate.FirstHopLocation);
		out << ",\"static_walk_first_hop_distance_known\":"
			<< (certificate.FirstHopDistanceKnown ? "true" : "false")
			<< ",\"static_walk_current_first_hop_probe_known\":"
			<< (certificate.CurrentFirstHopProbeKnown ? "true" : "false")
			<< ",\"static_walk_current_first_hop_probe_clear\":"
			<< (certificate.CurrentFirstHopProbeClear ? "true" : "false")
			<< ",\"static_walk_first_hop_entry_distance\":"
			<< Fixed(certificate.FirstHopEntryDistance, 6)
			<< ",\"static_walk_minimum_first_hop_distance\":"
			<< Fixed(certificate.MinimumFirstHopDistance, 6)
			<< ",\"static_walk_terminal_first_hop_distance\":"
			<< Fixed(certificate.TerminalFirstHopDistance, 6)
			<< ",\"static_walk_first_hop_progress_samples\":\""
			<< certificate.FirstHopProgressSamples
			<< "\",\"static_walk_first_hop_regression_samples\":\""
			<< certificate.FirstHopRegressionSamples << "\""
			<< ",\"static_walk_continuation_known\":"
			<< (certificate.ContinuationKnown ? "true" : "false")
			<< ",\"static_walk_continuation_name\":"
			<< JsonString(certificate.ContinuationName)
			<< ",\"static_walk_cost\":" << Fixed(certificate.StaticWalkCost, 6)
			<< ",\"static_walk_hops\":\"" << certificate.StaticWalkHops
			<< "\",\"static_walk_visited_nodes\":\"" << certificate.VisitedNodes
			<< "\"";
		out << ",\"candidate_known\":"
			<< (diagnostic.CandidateKnown ? "true" : "false")
			<< ",\"candidate_name\":" << JsonString(diagnostic.Candidate.Name)
			<< ",\"candidate_location\":";
		WriteVector(out, diagnostic.Candidate.Location);
		out << ",\"candidate_entry_distance\":"
			<< Fixed(diagnostic.Candidate.EntryDistance, 6)
			<< ",\"candidate_distance_known\":"
			<< (diagnostic.CandidateDistanceKnown ? "true" : "false")
			<< ",\"minimum_candidate_distance\":"
			<< Fixed(diagnostic.MinimumCandidateDistance, 6)
			<< ",\"terminal_candidate_distance\":"
			<< Fixed(diagnostic.TerminalCandidateDistance, 6)
			<< ",\"candidate_progress_samples\":\""
			<< diagnostic.CandidateProgressSamples
			<< "\",\"candidate_regression_samples\":\""
			<< diagnostic.CandidateRegressionSamples
			<< "\",\"target_distance_known\":"
			<< (diagnostic.TargetDistanceKnown ? "true" : "false")
			<< ",\"entry_target_distance\":"
			<< Fixed(diagnostic.EntryTargetDistance, 6)
			<< ",\"minimum_target_distance\":"
			<< Fixed(diagnostic.MinimumTargetDistance, 6)
			<< ",\"terminal_target_distance\":"
			<< Fixed(diagnostic.TerminalTargetDistance, 6)
			<< ",\"target_progress_samples\":\""
			<< diagnostic.TargetProgressSamples
			<< "\",\"target_regression_samples\":\""
			<< diagnostic.TargetRegressionSamples
			<< "\",\"terminal\":" << JsonString(
				PawnMovement::HazardWaterEgressTerminalName(diagnostic.Terminal))
			<< ",\"terminal_location\":";
		WriteVector(out, diagnostic.TerminalLocation);
		out << ",\"terminal_move_target_name\":"
			<< JsonString(diagnostic.TerminalMoveTargetName)
			<< ",\"terminal_destination\":";
		WriteVector(out, diagnostic.TerminalDestination);
		out << '}';
	}

	void WriteHazardDeathPartitionRecord(std::ostringstream& out,
		const BotBenchmarkHazardDeathPartitionRecord& record)
	{
		out << "{\"source_pawn_actor\":" << JsonString(record.SourcePawnActor)
			<< ",\"sequence\":\"" << record.Sequence
			<< "\",\"death_time_seconds\":" << Fixed(record.DeathTimeSeconds, 6)
			<< ",\"killer_relation\":" << JsonString(record.KillerRelation)
			<< ",\"attribution\":" << JsonString(record.Attribution)
			<< ",\"environmental_source\":" << JsonString(record.EnvironmentalSource)
			<< ",\"had_recent_enemy_contribution\":"
			<< (record.HadRecentEnemyContribution ? "true" : "false")
			<< ",\"had_recent_enemy_momentum_contribution\":"
			<< (record.HadRecentEnemyMomentumContribution ? "true" : "false")
			<< ",\"hazard_prefix\":" << JsonString(record.HazardPrefix)
			<< ",\"hazard_residence_terminal_exact\":"
			<< (record.HazardResidenceTerminalExact ? "true" : "false")
			<< ",\"hazard_residence_terminal\":"
			<< JsonString(record.HazardResidenceTerminal)
			<< ",\"hazard_residence_entry_health\":"
			<< record.HazardResidenceEntryHealth
			<< ",\"hazard_residence_harmful_seconds\":"
			<< Fixed(record.HazardResidenceHarmfulSeconds, 6)
			<< ",\"hazard_residence_command_changes\":\""
			<< record.HazardResidenceCommandChanges
			<< "\",\"hazard_residence_direct_safe_candidate_observed\":"
			<< (record.HazardResidenceDirectSafeCandidateObserved ? "true" : "false")
			<< ",\"hazard_residence_direct_safe_candidate_superseded\":"
			<< (record.HazardResidenceDirectSafeCandidateSuperseded ? "true" : "false")
			<< ",\"hazard_residence_direct_safe_candidate_name\":"
			<< JsonString(record.HazardResidenceDirectSafeCandidateName)
			<< ",\"hazard_residence_command_ownership_life_id\":\""
			<< record.HazardResidenceCommandOwnershipLifeId
			<< "\",\"hazard_residence_command_ownership_exact\":"
			<< (record.HazardResidenceCommandOwnershipExact ? "true" : "false")
			<< ",\"hazard_residence_command_ownership_target_name\":"
			<< JsonString(record.HazardResidenceCommandOwnershipTargetName)
			<< ",\"move_target_known\":"
			<< (record.MoveTargetKnown ? "true" : "false")
			<< ",\"move_target_name\":" << JsonString(record.MoveTargetName)
			<< ",\"movement_intent\":" << (record.MovementIntent ? "true" : "false")
			<< ",\"physics_mode\":" << JsonString(record.PhysicsMode)
			<< ",\"water_egress_terminal_known\":"
			<< (record.WaterEgressTerminalKnown ? "true" : "false")
			<< ",\"water_egress_sequence\":\"" << record.WaterEgressSequence
			<< "\",\"water_egress_life_id\":\"" << record.WaterEgressLifeId
			<< "\",\"water_egress_episode_id\":\"" << record.WaterEgressEpisodeId
			<< "\",\"falling_hazard_terminal_known\":"
			<< (record.FallingHazardTerminalKnown ? "true" : "false")
			<< ",\"falling_hazard_sequence\":\"" << record.FallingHazardSequence
			<< "\",\"falling_hazard_life_id\":\"" << record.FallingHazardLifeId
			<< "\",\"falling_hazard_fall_episode_id\":\""
			<< record.FallingHazardFallEpisodeId
			<< "\",\"falling_hazard_generation_id\":\""
			<< record.FallingHazardGenerationId
			<< "\",\"falling_hazard_correlation\":"
			<< JsonString(record.FallingHazardCorrelation)
			<< ",\"falling_parity_terminal_known\":"
			<< (record.FallingParityTerminalKnown ? "true" : "false")
			<< ",\"falling_parity_life_generation\":\""
			<< record.FallingParityLifeGeneration
			<< "\",\"falling_parity_invocation_token\":\""
			<< record.FallingParityInvocationToken
			<< "\",\"falling_parity_walking_iteration\":"
			<< record.FallingParityWalkingIteration << '}';
	}

	void WriteBot(std::ostringstream& out, const BotBenchmarkBotState& bot,
		bool targetSelectionObserverRequested,
		bool pickTargetObserverRequested,
		bool inventoryDirectReachSupportObserverRequested,
		bool directReachCommandObserverRequested)
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
		;
		if (targetSelectionObserverRequested)
		{
			out << ",\"target_selection_outermost_calls_exact\":\""
			<< bot.TargetSelectionOutermostCallsExact << "\""
			<< ",\"target_selection_nested_calls_exact\":\""
			<< bot.TargetSelectionNestedCallsExact << "\""
			<< ",\"target_selection_accepted_target_changes_exact\":\""
			<< bot.TargetSelectionAcceptedTargetChangesExact << "\""
			<< ",\"target_selection_accepted_same_target_exact\":\""
			<< bot.TargetSelectionAcceptedSameTargetExact << "\""
			<< ",\"target_selection_rejected_or_unchanged_exact\":\""
			<< bot.TargetSelectionRejectedOrUnchangedExact << "\""
			<< ",\"target_selection_missing_results_exact\":\""
			<< bot.TargetSelectionMissingResultsExact << "\""
			<< ",\"target_selection_invalid_identifier_exact\":\""
			<< bot.TargetSelectionInvalidIdentifierExact << "\""
			<< ",\"target_selection_tracker_capacity_exceeded_exact\":\""
			<< bot.TargetSelectionTrackerCapacityExceededExact << "\""
			<< ",\"target_selection_record_overflows_exact\":\""
			<< bot.TargetSelectionRecordOverflowsExact << "\""
			<< ",\"target_selection_integrity_failures_exact\":\""
			<< bot.TargetSelectionIntegrityFailuresExact << "\""
			<< ",\"target_selection_records\":[";
		for (size_t index = 0; index < bot.TargetSelectionRecords.size(); index++)
		{
			if (index) out << ',';
			const auto& record = bot.TargetSelectionRecords[index];
			out << "{\"sequence\":\"" << record.Sequence
				<< "\",\"contract_id\":" << JsonString(record.ContractId)
				<< ",\"bot_id\":" << JsonString(record.BotId)
				<< ",\"previous_target_id\":" << JsonString(record.PreviousTargetId)
				<< ",\"requested_target_id\":" << JsonString(record.RequestedTargetId)
				<< ",\"observed_target_id\":" << JsonString(record.ObservedTargetId)
				<< ",\"outcome\":" << JsonString(record.Outcome) << '}';
		}
			out << ']';
		}
		if (pickTargetObserverRequested)
		{
			out << ",\"pick_target_observations_exact\":\""
				<< bot.PickTargetObservationsExact << "\""
				<< ",\"pick_target_candidates_exact\":\"" << bot.PickTargetCandidatesExact << "\""
				<< ",\"pick_target_self_rejects_exact\":\"" << bot.PickTargetSelfRejectsExact << "\""
				<< ",\"pick_target_dead_rejects_exact\":\"" << bot.PickTargetDeadRejectsExact << "\""
				<< ",\"pick_target_living_skipped_by_current_predicate_exact\":\""
				<< bot.PickTargetLivingSkippedByCurrentPredicateExact << "\""
				<< ",\"pick_target_team_rejects_exact\":\"" << bot.PickTargetTeamRejectsExact << "\""
				<< ",\"pick_target_living_geometry_eligible_exact\":\""
				<< bot.PickTargetLivingGeometryEligibleExact << "\""
				<< ",\"pick_target_living_line_of_sight_eligible_exact\":\""
				<< bot.PickTargetLivingLineOfSightEligibleExact << "\""
				<< ",\"pick_target_returned_targets_exact\":\""
				<< bot.PickTargetReturnedTargetsExact << "\""
				<< ",\"pick_target_returned_living_targets_exact\":\""
				<< bot.PickTargetReturnedLivingTargetsExact << "\""
				<< ",\"pick_target_no_result_with_living_line_of_sight_candidate_exact\":\""
				<< bot.PickTargetNoResultWithLivingLineOfSightCandidateExact << "\""
				<< ",\"pick_target_observation_overflows_exact\":\""
				<< bot.PickTargetObservationOverflowsExact << "\""
				<< ",\"pick_target_integrity_failures_exact\":\""
				<< bot.PickTargetIntegrityFailuresExact << "\""
				<< ",\"pick_target_records\":[";
			for (size_t index = 0; index < bot.PickTargetRecords.size(); index++)
			{
				if (index) out << ',';
				const auto& record = bot.PickTargetRecords[index];
				out << "{\"sequence\":\"" << record.Sequence
					<< "\",\"candidate_pawns\":" << record.CandidatePawns
					<< ",\"self_rejects\":" << record.SelfRejects
					<< ",\"dead_rejects\":" << record.DeadRejects
					<< ",\"living_skipped_by_current_predicate\":"
					<< record.LivingSkippedByCurrentPredicate
					<< ",\"team_rejects\":" << record.TeamRejects
					<< ",\"living_geometry_eligible\":" << record.LivingGeometryEligible
					<< ",\"living_line_of_sight_eligible\":"
					<< record.LivingLineOfSightEligible
					<< ",\"returned_target\":" << (record.ReturnedTarget ? "true" : "false")
					<< ",\"returned_living_target\":"
					<< (record.ReturnedLivingTarget ? "true" : "false")
					<< ",\"no_result_with_living_line_of_sight_candidate\":"
					<< (record.NoResultWithLivingLineOfSightCandidate ? "true" : "false")
					<< ",\"integrity_valid\":" << (record.IntegrityValid ? "true" : "false")
					<< ",\"selected_actor\":" << JsonString(record.SelectedActor)
					<< ",\"selected_class\":" << JsonString(record.SelectedClass) << '}';
			}
			out << ']';
		}
		if (inventoryDirectReachSupportObserverRequested)
		{
			out << ",\"inventory_direct_reach_support_observations_exact\":\""
				<< bot.InventoryDirectReachSupportObservationsExact << "\""
				<< ",\"inventory_direct_reach_support_safe_supported_exact\":\""
				<< bot.InventoryDirectReachSupportSafeSupportedExact << "\""
				<< ",\"inventory_direct_reach_support_safe_unsupported_no_observed_hazard_exact\":\""
				<< bot.InventoryDirectReachSupportSafeUnsupportedNoObservedHazardExact << "\""
				<< ",\"inventory_direct_reach_support_unsafe_harmful_foot_zone_exact\":\""
				<< bot.InventoryDirectReachSupportUnsafeHarmfulFootZoneExact << "\""
				<< ",\"inventory_direct_reach_support_unsafe_unsupported_over_harmful_exact\":\""
				<< bot.InventoryDirectReachSupportUnsafeUnsupportedOverHarmfulExact << "\""
				<< ",\"inventory_direct_reach_support_unavailable_exact\":\""
				<< bot.InventoryDirectReachSupportUnavailableExact << "\""
			<< ",\"inventory_direct_reach_support_diagnostic_overflows_exact\":\""
				<< bot.InventoryDirectReachSupportDiagnosticOverflowsExact << "\""
				<< ",\"inventory_marker_direct_reach_rejects_exact\":\""
				<< bot.InventoryMarkerDirectReachRejectsExact << "\""
				<< ",\"inventory_direct_reach_support_diagnostics\":[";
			for (size_t index = 0; index < bot.InventoryDirectReachSupportDiagnostics.size(); index++)
			{
				if (index) out << ',';
				const auto& record = bot.InventoryDirectReachSupportDiagnostics[index];
				out << "{\"source_pawn_actor\":" << JsonString(record.SourcePawnActor)
					<< ",\"target_actor\":" << JsonString(record.TargetActor)
					<< ",\"target_class\":" << JsonString(record.TargetClass)
					<< ",\"sequence\":\"" << record.Sequence << "\""
					<< ",\"walking_simulation_iterations\":"
					<< record.WalkingSimulationIterations
					<< ",\"support_fraction\":" << Fixed(record.SupportFraction, 6)
					<< ",\"support_normal_z\":" << Fixed(record.SupportNormalZ, 6)
					<< ",\"walkable_support\":"
					<< (record.WalkableSupport ? "true" : "false")
					<< ",\"harmful_foot_zone\":"
					<< (record.HarmfulFootZone ? "true" : "false")
					<< ",\"harmful_below\":"
					<< (record.HarmfulBelow ? "true" : "false")
					<< ",\"outcome\":" << JsonString(
						PawnMovement::InventoryDirectReachSupportOutcomeName(record.Outcome)) << '}';
			}
			out << ']';
		}
		if (directReachCommandObserverRequested)
		{
			out << ",\"direct_reach_command_observations_exact\":\""
				<< bot.DirectReachCommandObservationsExact << "\""
				<< ",\"direct_reach_command_successes_exact\":\""
				<< bot.DirectReachCommandSuccessesExact << "\""
				<< ",\"direct_reach_command_failures_exact\":\""
				<< bot.DirectReachCommandFailuresExact << "\""
				<< ",\"direct_reach_command_same_life_exact_exact\":\""
				<< bot.DirectReachCommandSameLifeExactExact << "\""
				<< ",\"direct_reach_command_unlinked_exact\":\""
				<< bot.DirectReachCommandUnlinkedExact << "\""
				<< ",\"direct_reach_command_overflows_exact\":\""
				<< bot.DirectReachCommandOverflowsExact << "\""
				<< ",\"direct_reach_command_hazardous_deaths_exact\":\""
				<< bot.DirectReachCommandHazardousDeathsExact << "\""
				<< ",\"direct_reach_command_nonhazard_deaths_exact\":\""
				<< bot.DirectReachCommandNonhazardDeathsExact << "\""
				<< ",\"direct_reach_command_cleared_exact\":\""
				<< bot.DirectReachCommandClearedExact << "\""
				<< ",\"direct_reach_command_life_boundary_censored_exact\":\""
				<< bot.DirectReachCommandLifeBoundaryCensoredExact << "\""
				<< ",\"direct_reach_command_run_end_censored_exact\":\""
				<< bot.DirectReachCommandRunEndCensoredExact << "\""
				<< ",\"direct_reach_command_command_replaced_exact\":\""
				<< bot.DirectReachCommandCommandReplacedExact << "\""
				<< ",\"direct_reach_command_records\":[";
			for (size_t index = 0; index < bot.DirectReachCommandRecords.size(); index++)
			{
				if (index) out << ',';
				const auto& record = bot.DirectReachCommandRecords[index];
				out << "{\"sequence\":\"" << record.Sequence
					<< "\",\"life_id\":\"" << record.LifeId
					<< "\",\"reach_sequence\":\"" << record.ReachSequence
					<< "\",\"native_tick\":\"" << record.NativeTick
					<< "\",\"target_actor_index\":" << record.TargetActorIndex
					<< ",\"target_name\":" << JsonString(record.TargetName)
					<< ",\"target_class\":" << JsonString(record.TargetClass)
					<< ",\"target_is_inventory\":"
					<< (record.TargetIsInventory ? "true" : "false")
					<< ",\"marker_known\":" << (record.MarkerKnown ? "true" : "false")
					<< ",\"marker_live\":" << (record.MarkerLive ? "true" : "false")
					<< ",\"marker_actor_index\":" << record.MarkerActorIndex
					<< ",\"marker_name\":" << JsonString(record.MarkerName)
					<< ",\"marker_class\":" << JsonString(record.MarkerClass)
					<< ",\"reached\":" << (record.Reached ? "true" : "false")
					<< ",\"check_navpoint\":" << (record.CheckNavpoint ? "true" : "false")
					<< ",\"caller_origin\":" << JsonString(record.CallerOrigin)
					<< ",\"reject_reason\":" << JsonString(record.RejectReason)
					<< ",\"resolved_wall_slide\":"
					<< (record.ResolvedWallSlide ? "true" : "false")
					<< ",\"walking_simulation_iterations\":"
					<< record.WalkingSimulationIterations
					<< ",\"latent_action\":" << JsonString(record.LatentAction)
					<< ",\"route_head_present\":"
					<< (record.RouteHeadPresent ? "true" : "false")
					<< ",\"link_status\":" << JsonString(record.LinkStatus);
				if (record.LinkStatus == "same_life_exact")
				{
					out << ",\"activation_tick\":\"" << record.ActivationTick
						<< "\",\"terminal_tick\":\"" << record.TerminalTick
						<< "\",\"terminal\":" << JsonString(record.Terminal)
						<< ",\"hazard_terminal_exact\":"
						<< (record.HazardTerminalExact ? "true" : "false");
				}
				else
				{
					out << ",\"activation_tick\":null,\"terminal_tick\":null"
						<< ",\"terminal\":null,\"hazard_terminal_exact\":null";
				}
				out << '}';
			}
			out << ']';
		}
		out << ",\"environmental_deaths_exact\":\"" << bot.EnvironmentalDeathsExact << "\""
			<< ",\"hazard_exposed_deaths_proxy\":\"" << bot.HazardExposedDeathsProxy << "\""
			<< ",\"direct_self_kills\":\"" << bot.DirectSelfKills << "\""
			<< ",\"direct_enemy_kills\":\"" << bot.DirectEnemyKills << "\""
			<< ",\"unassisted_environmental_deaths\":\"" << bot.UnassistedEnvironmentalDeaths << "\""
			<< ",\"recent_enemy_contributed_environmental_deaths_proxy\":\"" << bot.RecentEnemyContributedEnvironmentalDeathsProxy << "\""
			<< ",\"ambiguous_deaths\":\"" << bot.AmbiguousDeaths << "\""
			<< ",\"recent_enemy_momentum_contributed_environmental_deaths_proxy\":\"" << bot.RecentEnemyMomentumContributedEnvironmentalDeathsProxy << "\""
			<< ",\"hit_wall_events_exact\":\"" << bot.HitWallEventsExact << "\""
			<< ",\"walking_hitwall_dispatch_observations_exact\":\"" << bot.WalkingHitWallDispatchObservationsExact << "\""
			<< ",\"walking_hitwall_dispatch_legacy_z_band_exact\":\"" << bot.WalkingHitWallDispatchLegacyZBandExact << "\""
			<< ",\"walking_hitwall_dispatch_minhitwall_exact\":\"" << bot.WalkingHitWallDispatchMinHitWallExact << "\""
			<< ",\"walking_hitwall_dispatch_disagreements_exact\":\"" << bot.WalkingHitWallDispatchDisagreementsExact << "\""
			<< ",\"walking_hitwall_dispatch_callbacks_exact\":\"" << bot.WalkingHitWallDispatchCallbacksExact << "\""
			<< ",\"walking_hitwall_dispatch_diagnostic_overflows_exact\":\"" << bot.WalkingHitWallDispatchDiagnosticOverflowsExact << "\""
			<< ",\"walking_hitwall_dispatch_diagnostics\":[";
		for (size_t index = 0; index < bot.WalkingHitWallDispatchDiagnostics.size(); index++)
		{
			if (index) out << ',';
			WriteWalkingHitWallDispatchDiagnostic(out,
				bot.WalkingHitWallDispatchDiagnostics[index]);
		}
		out << ']'
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
			<< ",\"move_stall_direct_actor_move_toward_timeouts_exact\":\"" << bot.MoveStallDirectActorMoveTowardTimeoutsExact << "\""
			<< ",\"move_stall_eligible_seconds\":" << Fixed(bot.MoveStallEligibleSeconds, 9)
			<< ",\"move_stall_recovery_episodes_exact\":\"" << bot.MoveStallRecoveryEpisodesExact << "\""
			<< ",\"move_stall_recovery_cleared_within_2_seconds_exact\":\"" << bot.MoveStallRecoveryClearedWithin2SecondsExact << "\""
			<< ",\"move_stall_recovery_cleared_after_2_seconds_within_5_seconds_exact\":\"" << bot.MoveStallRecoveryClearedAfter2SecondsWithin5SecondsExact << "\""
			<< ",\"move_stall_recovery_replanned_within_5_seconds_exact\":\"" << bot.MoveStallRecoveryReplannedWithin5SecondsExact << "\""
			<< ",\"move_stall_recovery_missed_5_second_deadline_exact\":\"" << bot.MoveStallRecoveryMissed5SecondDeadlineExact << "\""
			<< ",\"move_stall_recovery_excluded_intentional_stops_exact\":\"" << bot.MoveStallRecoveryExcludedIntentionalStopsExact << "\""
			<< ",\"move_stall_recovery_censored_life_boundaries_exact\":\"" << bot.MoveStallRecoveryCensoredLifeBoundariesExact << "\""
			<< ",\"move_stall_recovery_censored_run_end_exact\":\"" << bot.MoveStallRecoveryCensoredRunEndExact << "\""
			<< ",\"move_stall_recovery_unknown_exact\":\"" << bot.MoveStallRecoveryUnknownExact << "\""
			<< ",\"move_stall_recovery_episode_record_overflows_exact\":\"" << bot.MoveStallRecoveryEpisodeRecordOverflowsExact << "\""
			<< ",\"move_stall_recovery_episodes\":[";
		for (size_t index = 0; index < bot.MoveStallRecoveryEpisodes.size(); index++)
		{
			if (index) out << ',';
			WriteMoveStallRecoveryEpisode(out, bot.MoveStallRecoveryEpisodes[index]);
		}
		out << ']'
			<< ",\"move_stall_recovery_decision_record_overflows_exact\":\""
			<< bot.MoveStallRecoveryDecisionRecordOverflowsExact << "\""
			<< ",\"move_stall_recovery_decisions\":[";
		for (size_t index = 0; index < bot.MoveStallRecoveryDecisions.size(); index++)
		{
			if (index) out << ',';
			WriteMoveStallRecoveryDecision(out, bot.MoveStallRecoveryDecisions[index]);
		}
		out << ']'
			<< ",\"failed_navigation_avoidance_activations_exact\":\"" << bot.FailedNavigationAvoidanceActivationsExact << "\""
			<< ",\"failed_navigation_safeguard_suppressions_exact\":\"" << bot.FailedNavigationSafeguardSuppressionsExact << "\""
			<< ",\"failed_navigation_route_penalty_applications_exact\":\"" << bot.FailedNavigationRoutePenaltyApplicationsExact << "\""
			<< ",\"harmful_zone_escape_episodes_exact\":\"" << bot.HarmfulZoneEscapeEpisodesExact << "\""
			<< ",\"harmful_zone_escape_center_entries_exact\":\"" << bot.HarmfulZoneEscapeCenterEntriesExact << "\""
			<< ",\"harmful_zone_escape_foot_entries_exact\":\"" << bot.HarmfulZoneEscapeFootEntriesExact << "\""
			<< ",\"harmful_zone_escape_recovery_attempts_exact\":\"" << bot.HarmfulZoneEscapeRecoveryAttemptsExact << "\""
			<< ",\"pain_ledge_recovery_active_hitwall_events_exact\":\"" << bot.PainLedgeRecoveryActiveHitWallEventsExact << "\""
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
			<< ",\"hazard_swim_egress_forced_replan_same_command_reissued_exact\":\"" << bot.HazardSwimEgressForcedReplanSameCommandReissuedExact << "\""
			<< ",\"hazard_swim_egress_forced_replan_different_command_issued_exact\":\"" << bot.HazardSwimEgressForcedReplanDifferentCommandIssuedExact << "\""
			<< ",\"hazard_swim_egress_forced_replan_hazard_cleared_before_command_exact\":\"" << bot.HazardSwimEgressForcedReplanHazardClearedBeforeCommandExact << "\""
			<< ",\"hazard_swim_egress_forced_replan_fell_before_command_exact\":\"" << bot.HazardSwimEgressForcedReplanFellBeforeCommandExact << "\""
			<< ",\"hazard_swim_egress_forced_replan_died_before_command_exact\":\"" << bot.HazardSwimEgressForcedReplanDiedBeforeCommandExact << "\""
			<< ",\"hazard_swim_egress_forced_replan_life_boundary_censored_exact\":\"" << bot.HazardSwimEgressForcedReplanLifeBoundaryCensoredExact << "\""
			<< ",\"hazard_swim_egress_forced_replan_run_end_censored_exact\":\"" << bot.HazardSwimEgressForcedReplanRunEndCensoredExact << "\""
			<< ",\"hazard_swim_egress_forced_replan_episode_abandoned_exact\":\"" << bot.HazardSwimEgressForcedReplanEpisodeAbandonedExact << "\""
			<< ",\"hazard_swim_egress_falling_pre_move_anchor_captures_exact\":\"" << bot.HazardSwimEgressFallingPreMoveAnchorCapturesExact << "\""
			<< ",\"hazard_swim_egress_falling_pre_move_anchor_uses_exact\":\"" << bot.HazardSwimEgressFallingPreMoveAnchorUsesExact << "\""
			<< ",\"hazard_swim_egress_live_applies_exact\":\"" << bot.HazardSwimEgressLiveAppliesExact << "\""
			<< ",\"hazard_swim_egress_live_active_ticks_exact\":\"" << bot.HazardSwimEgressLiveActiveTicksExact << "\""
			<< ",\"hazard_swim_egress_live_probe_rejected_exact\":\"" << bot.HazardSwimEgressLiveProbeRejectedExact << "\""
			<< ",\"hazard_swim_egress_live_successful_exits_exact\":\"" << bot.HazardSwimEgressLiveSuccessfulExitsExact << "\""
			<< ",\"hazard_swim_egress_live_shadow_candidates_exact\":\"" << bot.HazardSwimEgressLiveShadowCandidatesExact << "\""
			<< ",\"hazard_swim_egress_live_shadow_falling_terminals_exact\":\"" << bot.HazardSwimEgressLiveShadowFallingTerminalsExact << "\""
			<< ",\"hazard_swim_egress_live_shadow_hazard_cleared_terminals_exact\":\"" << bot.HazardSwimEgressLiveShadowHazardClearedTerminalsExact << "\""
			<< ",\"hazard_swim_egress_live_shadow_probe_blocked_terminals_exact\":\"" << bot.HazardSwimEgressLiveShadowProbeBlockedTerminalsExact << "\""
			<< ",\"hazard_swim_egress_direct_nav_probes_exact\":\"" << bot.HazardSwimEgressDirectNavProbesExact << "\""
			<< ",\"hazard_swim_egress_direct_nav_safe_candidates_exact\":\"" << bot.HazardSwimEgressDirectNavSafeCandidatesExact << "\""
			<< ",\"hazard_residence_episodes_exact\":\"" << bot.HazardResidenceEpisodesExact << "\""
			<< ",\"hazard_residence_cleared_exact\":\"" << bot.HazardResidenceClearedExact << "\""
			<< ",\"hazard_residence_deaths_exact\":\"" << bot.HazardResidenceDeathsExact << "\""
			<< ",\"hazard_residence_life_boundary_censored_exact\":\"" << bot.HazardResidenceLifeBoundaryCensoredExact << "\""
			<< ",\"hazard_residence_run_end_censored_exact\":\"" << bot.HazardResidenceRunEndCensoredExact << "\""
			<< ",\"hazard_residence_unknown_exact\":\"" << bot.HazardResidenceUnknownExact << "\""
			<< ",\"hazard_residence_reentries_exact\":\"" << bot.HazardResidenceReentriesExact << "\""
			<< ",\"hazard_residence_command_changes_exact\":\"" << bot.HazardResidenceCommandChangesExact << "\""
			<< ",\"hazard_residence_candidates_observed_exact\":\"" << bot.HazardResidenceCandidatesObservedExact << "\""
			<< ",\"hazard_residence_candidate_other_commands_exact\":\"" << bot.HazardResidenceCandidateOtherCommandsExact << "\""
			<< ",\"hazard_swim_egress_direct_nav_best_candidate_name\":" << JsonString(bot.HazardSwimEgressDirectNavBestCandidateName)
			<< ",\"hazard_swim_egress_anchor_known\":" << (bot.HazardSwimEgressAnchorKnown ? "true" : "false")
			<< ",\"hazard_swim_egress_anchor_source\":" << JsonString(bot.HazardSwimEgressAnchorSource)
			<< ",\"hazard_water_egress_diagnostic_overflows_exact\":\""
			<< bot.HazardWaterEgressDiagnosticOverflowsExact << "\""
			<< ",\"hazard_water_egress_diagnostics\":[";
		for (size_t index = 0; index < bot.HazardWaterEgressDiagnostics.size(); index++)
		{
			if (index) out << ',';
			WriteHazardWaterEgressDiagnostic(out, bot.HazardWaterEgressDiagnostics[index]);
		}
		out << ']'
			<< ",\"hazard_death_partition_records\":[";
		for (size_t index = 0; index < bot.HazardDeathPartitionRecords.size(); index++)
		{
			if (index) out << ',';
			WriteHazardDeathPartitionRecord(out, bot.HazardDeathPartitionRecords[index]);
		}
		out << ']'
			<< ",\"falling_hazard_recovery_promotions_exact\":\""
			<< bot.FallingHazardRecoveryPromotionsExact << "\""
			<< ",\"falling_hazard_recovery_advance_calls_exact\":\""
			<< bot.FallingHazardRecoveryAdvanceCallsExact << "\""
			<< ",\"falling_hazard_recovery_context_rejected_exact\":\""
			<< bot.FallingHazardRecoveryContextRejectedExact << "\""
			<< ",\"falling_hazard_recovery_no_active_fall_episode_exact\":\""
			<< bot.FallingHazardRecoveryNoActiveFallEpisodeExact << "\""
			<< ",\"falling_hazard_recovery_no_prefix_exact\":\""
			<< bot.FallingHazardRecoveryNoPrefixExact << "\""
			<< ",\"falling_hazard_recovery_eligible_exact\":\""
			<< bot.FallingHazardRecoveryEligibleExact << "\""
			<< ",\"falling_hazard_recovery_anchor_rejected_exact\":\""
			<< bot.FallingHazardRecoveryAnchorRejectedExact << "\""
			<< ",\"falling_hazard_recovery_probe_rejected_exact\":\""
			<< bot.FallingHazardRecoveryProbeRejectedExact << "\""
			<< ",\"falling_hazard_recovery_live_applies_exact\":\""
			<< bot.FallingHazardRecoveryLiveAppliesExact << "\""
			<< ",\"falling_hazard_recovery_live_active_ticks_exact\":\""
			<< bot.FallingHazardRecoveryLiveActiveTicksExact << "\""
			<< ",\"falling_hazard_recovery_safe_landings_exact\":\""
			<< bot.FallingHazardRecoverySafeLandingsExact << "\""
			<< ",\"falling_hazard_recovery_harmful_entries_exact\":\""
			<< bot.FallingHazardRecoveryHarmfulEntriesExact << "\""
			<< ",\"falling_hazard_recovery_deaths_exact\":\""
			<< bot.FallingHazardRecoveryDeathsExact << "\""
			<< ",\"falling_hazard_recovery_timeouts_exact\":\""
			<< bot.FallingHazardRecoveryTimeoutsExact << "\""
			<< ",\"external_impulse_fall_harmful_witnesses_exact\":\""
			<< bot.ExternalImpulseFallHarmfulWitnessesExact << "\""
			<< ",\"external_impulse_fall_no_air_control_exact\":\""
			<< bot.ExternalImpulseFallNoAirControlExact << "\""
			<< ",\"external_impulse_fall_alternatives_tested_exact\":\""
			<< bot.ExternalImpulseFallAlternativesTestedExact << "\""
			<< ",\"external_impulse_fall_certified_exact\":\""
			<< bot.ExternalImpulseFallCertifiedExact << "\""
			<< ",\"external_impulse_fall_uncertified_exact\":\""
			<< bot.ExternalImpulseFallUncertifiedExact << "\""
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
			<< ",\"persistent_harmful_fall_candidates_started_exact\":\""
			<< bot.PersistentHarmfulFallCandidatesStartedExact << "\""
			<< ",\"persistent_harmful_fall_promotions_exact\":\""
			<< bot.PersistentHarmfulFallPromotionsExact << "\""
			<< ",\"persistent_harmful_fall_resets_exact\":\""
			<< bot.PersistentHarmfulFallResetsExact << "\""
			<< ",\"persistent_harmful_fall_confirmed_harmful_entries_exact\":\""
			<< bot.PersistentHarmfulFallConfirmedHarmfulEntriesExact << "\""
			<< ",\"persistent_harmful_fall_observed_lead_samples_exact\":\""
			<< bot.PersistentHarmfulFallObservedLeadSamplesExact << "\""
			<< ",\"persistent_harmful_fall_observed_lead_milliseconds_exact\":\""
			<< bot.PersistentHarmfulFallObservedLeadMillisecondsExact << "\""
			<< ",\"single_harmful_fall_prefix_candidates_started_exact\":\""
			<< bot.SingleHarmfulFallPrefixCandidatesStartedExact << "\""
			<< ",\"single_harmful_fall_prefix_promotions_exact\":\""
			<< bot.SingleHarmfulFallPrefixPromotionsExact << "\""
			<< ",\"single_harmful_fall_prefix_resets_exact\":\""
			<< bot.SingleHarmfulFallPrefixResetsExact << "\""
			<< ",\"single_harmful_fall_prefix_confirmed_harmful_entries_exact\":\""
			<< bot.SingleHarmfulFallPrefixConfirmedHarmfulEntriesExact << "\""
			<< ",\"single_harmful_fall_prefix_observed_lead_samples_exact\":\""
			<< bot.SingleHarmfulFallPrefixObservedLeadSamplesExact << "\""
			<< ",\"single_harmful_fall_prefix_observed_lead_milliseconds_exact\":\""
			<< bot.SingleHarmfulFallPrefixObservedLeadMillisecondsExact << "\""
			<< ",\"direct_harmful_water_entry_candidates_exact\":\""
			<< bot.DirectHarmfulWaterEntryCandidatesExact << "\""
			<< ",\"direct_harmful_water_entry_confirmed_exact\":\""
			<< bot.DirectHarmfulWaterEntryConfirmedExact << "\""
			<< ",\"direct_harmful_water_entry_confirmed_no_harm_exact\":\""
			<< bot.DirectHarmfulWaterEntryConfirmedNoHarmExact << "\""
			<< ",\"direct_harmful_water_entry_unresolved_exact\":\""
			<< bot.DirectHarmfulWaterEntryUnresolvedExact << "\""
			<< ",\"direct_harmful_water_entry_lead_samples_exact\":\""
			<< bot.DirectHarmfulWaterEntryLeadSamplesExact << "\""
			<< ",\"direct_harmful_water_entry_lead_milliseconds_exact\":\""
			<< bot.DirectHarmfulWaterEntryLeadMillisecondsExact << "\""
			<< ",\"direct_harmful_water_entry_certificate_source_not_eligible_exact\":\""
			<< bot.DirectHarmfulWaterEntryCertificateResultsExact[0] << "\""
			<< ",\"direct_harmful_water_entry_certificate_certified_exact\":\""
			<< bot.DirectHarmfulWaterEntryCertificateResultsExact[1] << "\""
			<< ",\"direct_harmful_water_entry_certificate_forecast_incomplete_or_inconsistent_exact\":\""
			<< bot.DirectHarmfulWaterEntryCertificateResultsExact[2] << "\""
			<< ",\"direct_harmful_water_entry_certificate_not_full_step_exact\":\""
			<< bot.DirectHarmfulWaterEntryCertificateResultsExact[3] << "\""
			<< ",\"direct_harmful_water_entry_certificate_not_harmful_water_endpoint_exact\":\""
			<< bot.DirectHarmfulWaterEntryCertificateResultsExact[4] << "\""
			<< ",\"direct_harmful_water_entry_certificate_damage_not_avoidance_relevant_exact\":\""
			<< bot.DirectHarmfulWaterEntryCertificateResultsExact[5] << "\""
			<< ",\"direct_harmful_water_entry_certificate_invalid_expected_harmful_zones_exact\":\""
			<< bot.DirectHarmfulWaterEntryCertificateResultsExact[6] << "\""
			<< ",\"direct_harmful_water_entry_certificate_unsafe_or_unknown_start_exact\":\""
			<< bot.DirectHarmfulWaterEntryCertificateResultsExact[7] << "\""
			<< ",\"direct_harmful_water_entry_certificate_intermediate_hazard_observed_exact\":\""
			<< bot.DirectHarmfulWaterEntryCertificateResultsExact[8] << "\""
			<< ",\"direct_harmful_water_entry_certificate_no_direct_clear_path_exact\":\""
			<< bot.DirectHarmfulWaterEntryCertificateResultsExact[9] << "\""
			<< ",\"direct_harmful_water_entry_certificate_invalid_prediction_accounting_exact\":\""
			<< bot.DirectHarmfulWaterEntryCertificateResultsExact[10] << "\""
			<< ",\"damage_taken_exact\":\"" << bot.DamageTakenExact << "\""
			<< ",\"damage_taken_from_other_participants_exact\":\""
			<< bot.DamageTakenFromOtherParticipantsExact << "\""
			<< ",\"damage_taken_from_self_exact\":\""
			<< bot.DamageTakenFromSelfExact << "\""
			<< ",\"damage_taken_from_nonparticipants_exact\":\""
			<< bot.DamageTakenFromNonParticipantsExact << "\""
			<< ",\"damage_dealt_to_other_participants_exact\":\""
			<< bot.DamageDealtToOtherParticipantsExact << "\""
			<< ",\"confirmed_pickups_exact\":\"" << bot.ConfirmedPickupsExact << "\""
			<< ",\"confirmed_weapon_pickups_exact\":\""
			<< bot.ConfirmedWeaponPickupsExact << "\""
			<< ",\"confirmed_ammo_pickups_exact\":\""
			<< bot.ConfirmedAmmoPickupsExact << "\""
			<< ",\"confirmed_health_pickups_exact\":\""
			<< bot.ConfirmedHealthPickupsExact << "\""
			<< ",\"confirmed_armor_pickups_exact\":\""
			<< bot.ConfirmedArmorPickupsExact << "\""
			<< ",\"confirmed_other_pickups_exact\":\""
			<< bot.ConfirmedOtherPickupsExact << "\""
			<< ",\"pickup_source_consumed_unconfirmed_exact\":\""
			<< bot.PickupSourceConsumedUnconfirmedExact << "\""
			<< ",\"navigation_coverage_visited_nodes_exact\":\""
			<< bot.NavigationCoverageVisitedNodesExact << "\""
			<< ",\"navigation_coverage_catalog_nodes_exact\":\""
			<< bot.NavigationCoverageCatalogNodesExact << "\""
			<< ",\"navigation_coverage_union_visited_nodes_exact\":\""
			<< bot.NavigationCoverageUnionVisitedNodesExact << "\""
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
		<< (config.IsHazardSwimEgressLiveEnabled() ? "1" : "0") << '\n'
		<< "failed_navigation_avoidance_enabled="
		<< (config.IsFailedNavigationAvoidanceEnabled() ? "1" : "0") << '\n'
		<< "falling_hazard_recovery_enabled="
		<< (config.IsFallingHazardRecoveryEnabled() ? "1" : "0") << '\n'
		<< "falling_hazard_recovery_live_enabled="
		<< (config.IsFallingHazardRecoveryLiveEnabled() ? "1" : "0") << '\n'
		<< "targetless_move_to_timeout_enabled="
		<< (config.IsTargetlessMoveToTimeoutEnabled() ? "1" : "0") << '\n'
		<< "direct_actor_move_toward_timeout_enabled="
		<< (config.IsDirectActorMoveTowardTimeoutEnabled() ? "1" : "0") << '\n'
		<< "target_selection_observer_enabled="
		<< (config.IsTargetSelectionObserverEnabled() ? "1" : "0") << '\n'
		<< "pick_target_observer_enabled="
		<< (config.IsPickTargetObserverEnabled() ? "1" : "0") << '\n'
		<< "inventory_direct_reach_support_observer_enabled="
		<< (config.IsInventoryDirectReachSupportObserverEnabled() ? "1" : "0") << '\n'
		<< "inventory_marker_direct_reach_safety_enabled="
		<< (config.IsInventoryMarkerDirectReachSafetyEnabled() ? "1" : "0") << '\n'
		<< "native_path_commit_observer_enabled="
		<< (config.IsNativePathCommitObserverEnabled() ? "1" : "0") << '\n'
		<< "direct_reach_command_observer_enabled="
		<< (config.IsDirectReachCommandObserverEnabled() ? "1" : "0") << '\n';
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
		<< "  \"failed_navigation_avoidance_enabled\": "
		<< (config.IsFailedNavigationAvoidanceEnabled() ? "true" : "false") << ",\n"
		<< "  \"falling_hazard_recovery_enabled\": "
		<< (config.IsFallingHazardRecoveryEnabled() ? "true" : "false") << ",\n"
		<< "  \"falling_hazard_recovery_live_enabled\": "
		<< (config.IsFallingHazardRecoveryLiveEnabled() ? "true" : "false") << ",\n"
		<< "  \"targetless_move_to_timeout_enabled\": "
		<< (config.IsTargetlessMoveToTimeoutEnabled() ? "true" : "false") << ",\n"
		<< "  \"direct_actor_move_toward_timeout_enabled\": "
		<< (config.IsDirectActorMoveTowardTimeoutEnabled() ? "true" : "false") << ",\n"
		<< "  \"target_selection_observer_enabled\": "
		<< (config.IsTargetSelectionObserverEnabled() ? "true" : "false") << ",\n"
		<< "  \"pick_target_observer_enabled\": "
		<< (config.IsPickTargetObserverEnabled() ? "true" : "false") << ",\n"
		<< "  \"inventory_direct_reach_support_observer_enabled\": "
		<< (config.IsInventoryDirectReachSupportObserverEnabled() ? "true" : "false") << ",\n"
		<< "  \"inventory_marker_direct_reach_safety_enabled\": "
		<< (config.IsInventoryMarkerDirectReachSafetyEnabled() ? "true" : "false") << ",\n"
		<< "  \"native_path_commit_observer_enabled\": "
		<< (config.IsNativePathCommitObserverEnabled() ? "true" : "false") << ",\n"
		<< "  \"direct_reach_command_observer_enabled\": "
		<< (config.IsDirectReachCommandObserverEnabled() ? "true" : "false") << ",\n"
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
		<< ",\"failure_reason\":" << JsonString(event.FailureReason);
	if (event.TargetSelectionObserverRequested)
	{
		out << ",\"target_selection_observer\":{\"requested\":true"
			<< ",\"status\":" << JsonString(event.TargetSelectionObserverStatus)
			<< ",\"reason\":" << JsonString(event.TargetSelectionObserverReason) << '}';
	}
	if (event.PickTargetObserverRequested)
		out << ",\"pick_target_observer\":{\"requested\":true,\"status\":\"active\"}";
	if (event.InventoryDirectReachSupportObserverRequested)
		out << ",\"inventory_direct_reach_support_observer\":{\"requested\":true,\"status\":\"active\"}";
	if (event.NativePathCommitObserverRequested)
		out << ",\"native_path_commit_observer\":{\"requested\":true,\"status\":\"active\"}";
	if (event.DirectReachCommandObserverRequested)
		out << ",\"direct_reach_command_observer\":{\"requested\":true,\"status\":\"active\"}";
	out << ",\"bots\":[";
	for (size_t index = 0; index < event.Bots.size(); index++)
	{
		if (index != 0)
			out << ',';
		WriteBot(out, event.Bots[index], event.TargetSelectionObserverRequested,
			event.PickTargetObserverRequested,
			event.InventoryDirectReachSupportObserverRequested,
			event.DirectReachCommandObserverRequested);
	}
	out << "]}\n";
	return out.str();
}
