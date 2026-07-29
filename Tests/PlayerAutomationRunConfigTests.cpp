#include "Automation/PlayerAutomationRunConfig.h"

#include <iostream>
#include <stdexcept>
#include <string>

using namespace Automation;

namespace
{
	void Check(bool condition, const char* message)
	{
		if (!condition)
			throw std::runtime_error(message);
	}

	template<typename Callback>
	void CheckRejects(Callback callback, const char* message)
	{
		try
		{
			callback();
		}
		catch (const std::invalid_argument&)
		{
			return;
		}
		throw std::runtime_error(message);
	}
}

int main()
{
	try
	{
		const PlayerAutomationRunConfig config = PlayerAutomationRunConfig::Parse(
			"01_NYC_UNATCOHQ", "qa-output", "100.5", "-20", "32", "48",
			"123", "600", "0.02");
		Check(config.GetURL() == "01_NYC_UNATCOHQ", "URL did not round trip");
		Check(config.GetOutputDirectory() == "qa-output", "output directory did not round trip");
		Check(config.GetSeed() == 123 && config.GetMaxTicks() == 600,
			"deterministic bounds did not round trip");
		Check(config.GetCommand().Kind == CommandKind::WalkToPoint,
			"run config did not create a walk-to-point command");
		Check(config.GetCommand().Point && config.GetCommand().Point->X == 100.5 &&
			config.GetCommand().Point->Y == -20.0 && config.GetCommand().Point->Z == 32.0,
			"target point did not round trip");
		Check(config.GetCommand().ArrivalRadius == 48.0,
			"arrival radius did not round trip");
		Check(config.ConfigIdentity().size() == 16,
			"config identity is not a bounded FNV-1a digest");
		const std::string manifest = config.ManifestJson();
		Check(manifest.find("surreal-player-automation-manifest-v1") != std::string::npos &&
			manifest.find("deterministic-player-automation") != std::string::npos &&
			manifest.find("walk_to_point") != std::string::npos,
			"manifest omitted required provenance");

		const PlayerAutomationRunConfig wait = PlayerAutomationRunConfig::Parse(
			"Training", "qa-output", {}, {}, {}, {}, {}, "10", {}, "wait", "3");
		Check(wait.GetCommand().Kind == CommandKind::Wait && wait.GetCommand().WaitTicks == 3 &&
			!wait.GetCommand().Point && !wait.GetCommand().ArrivalRadius,
			"wait action did not produce an exact bounded wait command");
		const PlayerAutomationRunConfig capturedWait = PlayerAutomationRunConfig::Parse(
			"Training", "qa-output", {}, {}, {}, {}, {}, "10", {}, "wait", "1",
			{}, {}, {}, {}, "1", "capture-session-1", std::string(40, 'c'), "true");
		Check(capturedWait.HasCaptureRequest() && capturedWait.GetCaptureRequest() &&
			capturedWait.GetCaptureRequest()->Tick == 1 &&
			capturedWait.GetCaptureRequest()->SessionId == "capture-session-1" &&
			capturedWait.GetCaptureRequest()->SourceRevision == std::string(40, 'c') &&
			capturedWait.GetCaptureRequest()->SourceDirty,
			"presented capture request did not preserve its exact provenance");
		Check(capturedWait.ManifestJson().find(
			"surreal-player-automation-manifest-v4") != std::string::npos &&
			capturedWait.ManifestJson().find(
				"deterministic-player-automation-visual-capture") != std::string::npos &&
			capturedWait.ManifestJson().find("\"tick\":\"1\"") != std::string::npos &&
			capturedWait.ManifestJson().find("\"source_dirty\":true") != std::string::npos,
			"capture manifest omitted its bounded render request");
		Check(capturedWait.ConfigIdentity() != wait.ConfigIdentity(),
			"capture request did not change the configuration identity");
		const PlayerAutomationRunConfig preActionWalk = PlayerAutomationRunConfig::Parse(
			"TrainingFinal", "qa-output", {}, {}, {}, {}, {}, "100", {},
			"walk_to_actor", {}, "actor:Engine.Light:Light155#0", "Engine.Light",
			{}, {}, "0", "pre-action-session", std::string(40, 'd'), "false",
			"pre_action");
		Check(preActionWalk.GetCaptureRequest() &&
			preActionWalk.GetCaptureRequest()->Tick == 0 &&
			preActionWalk.GetCaptureRequest()->Phase == AutomationCapturePhase::PreAction &&
			preActionWalk.ManifestJson().find(
				"surreal-player-automation-manifest-v5") != std::string::npos &&
			preActionWalk.ManifestJson().find(
				"surreal-player-automation-capture-request-v2") != std::string::npos &&
			preActionWalk.ManifestJson().find("\"phase\":\"pre_action\"") !=
				std::string::npos,
			"pre-action capture did not preserve its zero-input boundary contract");
		const PlayerAutomationRunConfig preStockInteract = PlayerAutomationRunConfig::Parse(
			"TrainingFinal", "qa-output", {}, {}, {}, "16", {}, "500", "0.02",
			"interact", {}, "actor:DeusEx.Switch1:Switch1#0", "DeusEx.Switch1",
			{}, {}, "248", "pre-stock-session", std::string(40, 'e'), "true",
			"pre_stock_interaction");
		Check(preStockInteract.GetCaptureRequest() &&
			preStockInteract.GetCaptureRequest()->Tick == 248 &&
			preStockInteract.GetCaptureRequest()->Phase ==
				AutomationCapturePhase::PreStockInteraction &&
			preStockInteract.ManifestJson().find(
				"\"phase\":\"pre_stock_interaction\"") != std::string::npos,
			"pre-stock-interaction capture did not preserve its exact action barrier");
		const PlayerAutomationRunConfig prePickup = PlayerAutomationRunConfig::Parse(
			"TrainingFinal", "qa-output", {}, {}, {}, "16", {}, "500", "0.02",
			"acquire_item", {}, "actor:DeusEx.TechGoggles:TechGoggles0#0",
			"DeusEx.TechGoggles", {}, {}, "470", "pre-pickup-session",
			std::string(40, 'f'), "true", "pre_pickup");
		Check(prePickup.GetCaptureRequest() &&
			prePickup.GetCaptureRequest()->Tick == 470 &&
			prePickup.GetCaptureRequest()->Phase == AutomationCapturePhase::PrePickup &&
			prePickup.ManifestJson().find("\"phase\":\"pre_pickup\"") !=
				std::string::npos,
			"pre-pickup capture did not preserve its exact acquisition barrier");

		const PlayerAutomationRunConfig abortedWait = PlayerAutomationRunConfig::Parse(
			"Training", "qa-output", {}, {}, {}, {}, {}, "10", {}, "wait", "8",
			{}, {}, "4");
		Check(abortedWait.GetAbortCommand() &&
			abortedWait.GetAbortCommand()->Kind == CommandKind::Abort &&
			abortedWait.GetAbortCommand()->IssuedTick == 4 &&
			abortedWait.GetAbortCommand()->DeadlineTick == 5 &&
			abortedWait.GetAbortCommand()->AbortCommandId == abortedWait.GetCommand().Id,
			"scheduled abort command did not preserve its bounded target and tick");
		Check(abortedWait.ManifestJson().find("\"abort_command\": {") != std::string::npos &&
			abortedWait.ManifestJson().find("\"kind\":\"abort\"") != std::string::npos,
			"manifest omitted the scheduled abort command");

		const PlayerAutomationRunConfig sightProbe = PlayerAutomationRunConfig::Parse(
			"TrainingFinal", "qa-output", {}, {}, {}, {}, {}, "1", {},
			"sight_probe", {}, "actor:DeusEx.Soldier:Soldier0#0", "DeusEx.Soldier");
		Check(sightProbe.IsSightProbe() && sightProbe.GetSightProbeTarget() &&
			sightProbe.GetSightProbeTarget()->ObservationRevision == 1 &&
			sightProbe.GetSightProbeTarget()->Identity ==
				"actor:DeusEx.Soldier:Soldier0#0" &&
			sightProbe.GetSightProbeTarget()->ExpectedClass == "DeusEx.Soldier",
			"sight probe did not preserve its exact separate target selector");
		Check(sightProbe.GetCommand().Kind == CommandKind::Wait &&
			sightProbe.GetCommand().Id == "deus-ex-sight-probe-1" &&
			sightProbe.GetCommand().WaitTicks == 1 && !sightProbe.GetCommand().Target,
			"sight probe expanded the gameplay command kind or target contract");
		Check(sightProbe.ManifestJson().find(
			"surreal-player-automation-manifest-v2") != std::string::npos &&
			sightProbe.ManifestJson().find("\"type\":\"deus_ex_ai_can_see\"") !=
				std::string::npos &&
			sightProbe.ManifestJson().find("\"check_visibility\":false") !=
				std::string::npos &&
			sightProbe.ManifestJson().find(
				"\"gameplay_property_mutation\":false,\"collision_bookkeeping_mutation\":true") !=
				std::string::npos &&
			sightProbe.ManifestJson().find(
				"\"evaluations\":[\"scalar\",\"direction\",\"los\",\"cylinder_los\"]") !=
				std::string::npos,
			"sight probe manifest omitted its versioned fixed call contract");
		const PlayerAutomationRunConfig otherSightTarget = PlayerAutomationRunConfig::Parse(
			"TrainingFinal", "qa-output", {}, {}, {}, {}, {}, "1", {},
			"sight_probe", {}, "actor:DeusEx.Soldier:Soldier2#0", "DeusEx.Soldier");
		Check(sightProbe.ConfigIdentity() != otherSightTarget.ConfigIdentity(),
			"different exact sight targets reused a configuration identity");
		Check(wait.ManifestJson().find("surreal-player-automation-manifest-v1") !=
			std::string::npos && !wait.IsSightProbe() &&
			wait.ManifestJson().find("\"probe\":") == std::string::npos,
			"non-probe manifest was changed into the probe schema");

		const PlayerAutomationRunConfig acquire = PlayerAutomationRunConfig::Parse(
			"Training", "qa-output", {}, {}, {}, "36", {}, "300", {},
			"acquire_item", {}, "actor:DeusEx.Multitool:Multitool0#0", "DeusEx.Multitool");
		Check(acquire.GetCommand().Kind == CommandKind::AcquireItem &&
			acquire.GetCommand().Target &&
			acquire.GetCommand().Target->ObservationRevision == 1 &&
			acquire.GetCommand().Target->Identity == "actor:DeusEx.Multitool:Multitool0#0" &&
			acquire.GetCommand().Target->ExpectedClass == "DeusEx.Multitool" &&
			acquire.GetCommand().ArrivalRadius == 36.0,
			"acquire-item action did not preserve its exact snapshot selector");

		const PlayerAutomationRunConfig acquisitionPair = PlayerAutomationRunConfig::Parse(
			"TrainingCombat", "qa-output", {}, {}, {}, "48", "104729", "4000",
			"0.02", "acquire_item", {},
			"actor:DeusEx.Ammo10mm:Ammo10mm0#0", "DeusEx.Ammo10mm", {},
			"actor:DeusEx.Ammo10mm:Ammo10mm1#0");
		Check(acquisitionPair.HasFollowupCommand() &&
			acquisitionPair.GetFollowupCommandTemplate() &&
			acquisitionPair.GetFollowupCommandTemplate()->Id == "acquire-item-2" &&
			acquisitionPair.GetFollowupCommandTemplate()->Kind == CommandKind::AcquireItem &&
			acquisitionPair.GetFollowupCommandTemplate()->Target &&
			acquisitionPair.GetFollowupCommandTemplate()->Target->Identity ==
				"actor:DeusEx.Ammo10mm:Ammo10mm1#0" &&
			acquisitionPair.GetFollowupCommandTemplate()->Target->ExpectedClass ==
				"DeusEx.Ammo10mm" &&
			acquisitionPair.GetFollowupCommandTemplate()->ArrivalRadius == 48.0,
			"same-class follow-up acquisition template was not preserved");
		const AutomationCommand materialized =
			acquisitionPair.MaterializeFollowupCommand(1431, 1432);
		Check(materialized.Id == "acquire-item-2" &&
			materialized.IssuedTick == 1431 && materialized.DeadlineTick == 4000 &&
			materialized.Target && materialized.Target->ObservationRevision == 1432 &&
			ValidateCommandForSnapshot(materialized, 1431, 1432),
			"follow-up acquisition was not rebound to its fresh observation and tick");
		const std::string pairManifest = acquisitionPair.ManifestJson();
		Check(pairManifest.find("surreal-player-automation-manifest-v3") !=
			std::string::npos &&
			pairManifest.find("deterministic-player-automation-sequence") !=
				std::string::npos &&
			pairManifest.find("\"followup_command_template\": {") !=
				std::string::npos &&
			pairManifest.find("\"followup_command_artifact\": \"followup-command.json\"") !=
				std::string::npos &&
			pairManifest.find("\"sequence_summary_artifact\": \"sequence-summary.json\"") !=
				std::string::npos,
			"sequence manifest omitted the bounded follow-up artifacts");
		const PlayerAutomationRunConfig otherPair = PlayerAutomationRunConfig::Parse(
			"TrainingCombat", "qa-output", {}, {}, {}, "48", "104729", "4000",
			"0.02", "acquire_item", {},
			"actor:DeusEx.Ammo10mm:Ammo10mm0#0", "DeusEx.Ammo10mm", {},
			"actor:DeusEx.Ammo10mm:Ammo10mm2#0");
		Check(acquisitionPair.ConfigIdentity() != otherPair.ConfigIdentity(),
			"different follow-up acquisition targets reused a configuration identity");

		const PlayerAutomationRunConfig walkActor = PlayerAutomationRunConfig::Parse(
			"TrainingFinal", "qa-output", {}, {}, {}, {}, {}, "300", {},
			"walk_to_actor", {}, "actor:Engine.Light:Light155#0", "Engine.Light");
		Check(walkActor.GetCommand().Kind == CommandKind::WalkToActor &&
			walkActor.GetCommand().Id == "walk-to-actor-1" &&
			walkActor.GetCommand().Target &&
			walkActor.GetCommand().Target->ObservationRevision == 1 &&
			walkActor.GetCommand().Target->Identity == "actor:Engine.Light:Light155#0" &&
			walkActor.GetCommand().Target->ExpectedClass == "Engine.Light" &&
			walkActor.GetCommand().ArrivalRadius == 40.0 &&
			walkActor.ManifestJson().find("\"kind\":\"walk_to_actor\"") != std::string::npos,
			"walk-to-actor action did not preserve its exact snapshot selector");

		const PlayerAutomationRunConfig defaults = PlayerAutomationRunConfig::Parse(
			"Training", "qa-output", "0", "0", "0", {}, {}, {}, {});
		Check(defaults.GetOutputDirectory() == "qa-output" && defaults.GetMaxTicks() == 900 &&
			defaults.GetCommand().ArrivalRadius == 40.0,
			"safe deterministic defaults changed");

		CheckRejects([] { PlayerAutomationRunConfig::Parse({}, {}, "0", "0", "0", {}, {}, {}, {}); },
			"missing URL was accepted");
		CheckRejects([] { PlayerAutomationRunConfig::Parse("Training", {}, "0", "0", "0", {}, {}, {}, {}); },
			"missing output directory was accepted");
		CheckRejects([] { PlayerAutomationRunConfig::Parse("Training", "qa-output", {}, "0", "0", {}, {}, {}, {}); },
			"missing target coordinate was accepted");
		CheckRejects([] { PlayerAutomationRunConfig::Parse("Training", "qa-output", "nan", "0", "0", {}, {}, {}, {}); },
			"non-finite target coordinate was accepted");
		CheckRejects([] { PlayerAutomationRunConfig::Parse("Training", "qa-output", "0", "0", "0", "0", {}, {}, {}); },
			"zero arrival radius was accepted");
		CheckRejects([] { PlayerAutomationRunConfig::Parse("Training", "qa-output", "0", "0", "0", {}, {}, "1000001", {}); },
			"unbounded tick lifetime was accepted");
		CheckRejects([] { PlayerAutomationRunConfig::Parse("Training", "qa-output", "0", {}, {}, {}, {}, "10", {}, "wait", "2"); },
			"wait accepted movement target fields");
		CheckRejects([] { PlayerAutomationRunConfig::Parse("Training", "qa-output", {}, {}, {}, {}, {}, "10", {}, "wait", "0"); },
			"zero-duration wait was accepted");
		CheckRejects([] { PlayerAutomationRunConfig::Parse("Training", "qa-output", {}, {}, {}, {}, {}, "10", {}, "attack", {}); },
			"unsupported action was accepted");
		CheckRejects([] { PlayerAutomationRunConfig::Parse("Training", "qa-output", {}, {}, {}, {}, {}, "10", {}, "acquire_item", {}); },
			"actor action without an exact selector was accepted");
		CheckRejects([] { PlayerAutomationRunConfig::Parse("Training", "qa-output", {}, {}, {}, {}, {}, "10", {}, "walk_to_actor", {}); },
			"walk-to-actor without an exact selector was accepted");
		CheckRejects([] { PlayerAutomationRunConfig::Parse("TrainingFinal", "qa-output", {}, {}, {}, {}, {}, "1", {},
			"sight_probe", {}, "actor:DeusEx.Soldier:Soldier0#0", {}); },
			"sight probe accepted a partial selector");
		CheckRejects([] { PlayerAutomationRunConfig::Parse("TrainingFinal", "qa-output", {}, {}, {}, {}, {}, "1", {},
			"sight_probe", {}, std::string(MaximumTargetIdentityBytes + 1, 'x'),
			"DeusEx.Soldier"); },
			"sight probe accepted an oversized identity");
		CheckRejects([] { PlayerAutomationRunConfig::Parse("TrainingFinal", "qa-output", "1", {}, {}, {}, {}, "1", {},
			"sight_probe", {}, "actor:DeusEx.Soldier:Soldier0#0", "DeusEx.Soldier"); },
			"sight probe accepted a point field");
		CheckRejects([] { PlayerAutomationRunConfig::Parse("TrainingFinal", "qa-output", {}, {}, {}, "40", {}, "1", {},
			"sight_probe", {}, "actor:DeusEx.Soldier:Soldier0#0", "DeusEx.Soldier"); },
			"sight probe accepted an arrival radius");
		CheckRejects([] { PlayerAutomationRunConfig::Parse("TrainingFinal", "qa-output", {}, {}, {}, {}, {}, "2", {},
			"sight_probe", "1", "actor:DeusEx.Soldier:Soldier0#0", "DeusEx.Soldier"); },
			"sight probe accepted an explicit wait duration");
		CheckRejects([] { PlayerAutomationRunConfig::Parse("TrainingFinal", "qa-output", {}, {}, {}, {}, {}, "2", {},
			"sight_probe", {}, "actor:DeusEx.Soldier:Soldier0#0", "DeusEx.Soldier", "1"); },
			"sight probe accepted a scheduled abort");
		CheckRejects([] { PlayerAutomationRunConfig::Parse("Training", "qa-output", "1", {}, {}, {}, {}, "10", {},
			"interact", {}, "actor:panel", "DeusEx.Switch1"); },
			"actor action accepted a point coordinate");
		CheckRejects([] { PlayerAutomationRunConfig::Parse("Training", "qa-output", {}, {}, {}, {}, {}, "10", {},
			"wait", "8", {}, {}, "0"); },
			"zero abort tick was accepted");
		CheckRejects([] { PlayerAutomationRunConfig::Parse("Training", "qa-output", {}, {}, {}, {}, {}, "10", {},
			"wait", "8", {}, {}, "11"); },
			"abort tick beyond the bounded run was accepted");
		CheckRejects([] { PlayerAutomationRunConfig::Parse("Training", "qa-output", {}, {}, {}, {}, {}, "10", {},
			"wait", "8", {}, {}, "10"); },
			"abort tick at the run limit was accepted");
		CheckRejects([] { PlayerAutomationRunConfig::Parse("TrainingCombat", "qa-output", {}, {}, {}, {}, {}, "100", {},
			"wait", "8", {}, {}, {}, "actor:DeusEx.Ammo10mm:Ammo10mm1#0"); },
			"non-acquisition action accepted a follow-up target");
		CheckRejects([] { PlayerAutomationRunConfig::Parse("TrainingCombat", "qa-output", {}, {}, {}, {}, {}, "100", {},
			"acquire_item", {}, "actor:DeusEx.Ammo10mm:Ammo10mm0#0", "DeusEx.Ammo10mm", {},
			"actor:DeusEx.Ammo10mm:Ammo10mm0#0"); },
			"follow-up acquisition accepted the primary target identity");
		CheckRejects([] { PlayerAutomationRunConfig::Parse("TrainingCombat", "qa-output", {}, {}, {}, {}, {}, "100", {},
			"acquire_item", {}, "actor:DeusEx.Ammo10mm:Ammo10mm0#0", "DeusEx.Ammo10mm", "50",
			"actor:DeusEx.Ammo10mm:Ammo10mm1#0"); },
			"follow-up acquisition accepted a scheduled abort");
		CheckRejects([] { PlayerAutomationRunConfig::Parse(
			"Training", "qa-output", {}, {}, {}, {}, {}, "10", {}, "wait", "1",
			{}, {}, {}, {}, "1"); },
			"partial presented capture provenance was accepted");
		CheckRejects([] { PlayerAutomationRunConfig::Parse(
			"Training", "qa-output", {}, {}, {}, {}, {}, "10", {}, "wait", "1",
			{}, {}, {}, {}, "0", "session", std::string(40, 'c'), "false"); },
			"zero presented capture tick was accepted");
		CheckRejects([] { PlayerAutomationRunConfig::Parse(
			"Training", "qa-output", {}, {}, {}, {}, {}, "10", {}, "wait", "1",
			{}, {}, {}, {}, "11", "session", std::string(40, 'c'), "false"); },
			"presented capture tick beyond the run was accepted");
		CheckRejects([] { PlayerAutomationRunConfig::Parse(
			"Training", "qa-output", {}, {}, {}, {}, {}, "10", {}, "wait", "1",
			{}, {}, {}, {}, "0", "session", std::string(40, 'c'), "false",
			"pre_action"); },
			"pre-action capture accepted a non-actor command");
		CheckRejects([] { PlayerAutomationRunConfig::Parse(
			"TrainingFinal", "qa-output", {}, {}, {}, {}, {}, "10", {},
			"walk_to_actor", {}, "actor:Engine.Light:Light155#0", "Engine.Light",
			{}, {}, "1", "session", std::string(40, 'c'), "false",
			"pre_action"); },
			"pre-action capture accepted a nonzero tick");
		CheckRejects([] { PlayerAutomationRunConfig::Parse(
			"TrainingFinal", "qa-output", {}, {}, {}, "16", {}, "500", {},
			"acquire_item", {}, "actor:DeusEx.TechGoggles:TechGoggles0#0",
			"DeusEx.TechGoggles", {}, {}, "470", "session", std::string(40, 'c'),
			"false", "pre_stock_interaction"); },
			"pre-stock-interaction capture accepted an acquisition with subcommand ambiguity");
		CheckRejects([] { PlayerAutomationRunConfig::Parse(
			"TrainingFinal", "qa-output", {}, {}, {}, "16", {}, "500", {},
			"acquire_item", {}, "actor:DeusEx.TechGoggles:TechGoggles0#0",
			"DeusEx.TechGoggles", {}, {}, "0", "session", std::string(40, 'c'),
			"false", "pre_action"); },
			"pre-action walk barrier accepted an acquisition command");
		CheckRejects([] { PlayerAutomationRunConfig::Parse(
			"TrainingFinal", "qa-output", {}, {}, {}, "16", {}, "500", {},
			"interact", {}, "actor:DeusEx.Switch1:Switch1#0", "DeusEx.Switch1",
			{}, {}, "0", "session", std::string(40, 'c'), "false", "pre_action"); },
			"pre-action walk barrier accepted an interaction command");
		CheckRejects([] { PlayerAutomationRunConfig::Parse(
			"TrainingFinal", "qa-output", {}, {}, {}, "16", {}, "500", {},
			"interact", {}, "actor:DeusEx.Switch1:Switch1#0", "DeusEx.Switch1",
			{}, {}, "248", "session", std::string(40, 'c'), "false", "pre_pickup"); },
			"pre-pickup acquisition barrier accepted an interaction command");
		CheckRejects([] { PlayerAutomationRunConfig::Parse(
			"TrainingFinal", "qa-output", {}, {}, {}, "40", {}, "100", {},
			"walk_to_actor", {}, "actor:Engine.Light:Light155#0", "Engine.Light",
			{}, {}, "53", "session", std::string(40, 'c'), "false", "pre_pickup"); },
			"pre-pickup acquisition barrier accepted a walk command");
		CheckRejects([] { PlayerAutomationRunConfig::Parse(
			"TrainingFinal", "qa-output", {}, {}, {}, "16", {}, "500", {},
			"interact", {}, "actor:DeusEx.Switch1:Switch1#0", "DeusEx.Switch1",
			{}, {}, {}, {}, {}, {}, "pre_stock_interaction"); },
			"capture phase without complete provenance was accepted");
		CheckRejects([] { PlayerAutomationRunConfig::Parse(
			"Training", "qa-output", {}, {}, {}, {}, {}, "10", {}, "wait", "1",
			{}, {}, {}, {}, "1", "session", std::string(40, 'C'), "false"); },
			"uppercase presented capture source revision was accepted");
		CheckRejects([] { PlayerAutomationRunConfig::Parse(
			"Training", "qa-output", {}, {}, {}, {}, {}, "10", {}, "wait", "1",
			{}, {}, {}, {}, "1", "session", std::string(40, 'c'), "unknown"); },
			"invalid presented capture dirty state was accepted");
		CheckRejects([] { PlayerAutomationRunConfig::Parse(
			"TrainingCombat", "qa-output", {}, {}, {}, "48", {}, "100", {},
			"acquire_item", {}, "actor:DeusEx.Ammo10mm:Ammo10mm0#0",
			"DeusEx.Ammo10mm", {}, "actor:DeusEx.Ammo10mm:Ammo10mm1#0",
			"1", "session", std::string(40, 'c'), "false"); },
			"follow-up acquisition accepted a presented capture request");
		CheckRejects([&] { (void)acquire.MaterializeFollowupCommand(1, 2); },
			"run without a follow-up command materialized one");
		CheckRejects([&] { (void)acquisitionPair.MaterializeFollowupCommand(4000, 4001); },
			"follow-up command materialized at the global tick limit");
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << '\n';
		return 1;
	}
	return 0;
}
