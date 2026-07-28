#include "Fixtures/ShadowReplayResultLoader.h"
#include "Fixtures/ShadowReplayFixture.h"

#include <nlohmann/json.hpp>

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>

using namespace Automation;
using namespace Automation::TestFixtures;

namespace
{
	using Json = nlohmann::json;
	int Failures = 0;

	void Check(bool condition, const std::string& message)
	{
		if (!condition)
		{
			std::cerr << "FAILED: " << message << '\n';
			Failures++;
		}
	}

	ExpectedShadowReplayBinding Binding()
	{
		return {
			"fixture-positive-v1",
			"capture-positive",
			std::string(64, '1'),
			std::string(64, '2'),
			std::string(64, '3'),
			"qwen3.5:9b",
			std::string(64, '4')
		};
	}

	Json ResultFor(const ExpectedShadowReplayBinding& binding,
		const std::string& action = "walk_to_actor",
		const std::string& identity = "actor:Engine.Light:Light155#0",
		const std::string& expectedClass = "Engine.Light",
		uint64_t waitTicks = 0)
	{
		Json candidate = nullptr;
		if (action != "none")
		{
			const bool wait = action == "wait";
			candidate = {
				{ "schema", "surreal-automation-shadow-candidate-v1" },
				{ "dispatch_authorized", false },
				{ "kind", action },
				{ "observation_revision", "7" },
				{ "target", wait ? Json(nullptr) : Json{
					{ "identity", identity }, { "expected_class", expectedClass } } },
				{ "arrival_radius", wait ? Json(nullptr) :
					Json(action == "walk_to_actor" ? 40 : 48) },
				{ "wait_ticks", waitTicks }
			};
		}
		return {
			{ "schema", "surreal-visual-qa-shadow-replay-result-v1" },
			{ "fixture_id", binding.FixtureId },
			{ "capture_id", binding.CaptureId },
			{ "fixture_sha256", binding.FixtureSha256 },
			{ "observation_sha256", binding.ObservationSha256 },
			{ "report_sha256", binding.ReportSha256 },
			{ "model", {
				{ "tag", binding.ModelTag }, { "digest", binding.ModelDigest } } },
			{ "verdict", "passed" },
			{ "reason", "proposal matched the curated acceptance oracle" },
			{ "proposal", {
				{ "action", action },
				{ "target_identity", action == "wait" || action == "none" ? "" : identity },
				{ "wait_ticks", waitTicks } } },
			{ "shadow_candidate", candidate },
			{ "dispatch_authorized", false },
			{ "controls_live_player", false }
		};
	}

	ObservationSnapshot Observation()
	{
		ObservationSnapshot observation;
		observation.Revision = 7;
		observation.Tick = 10;
		observation.PlayerIdentity = "actor:DeusEx.JCDentonMale:JCDentonMale0#0";

		TargetSnapshot light;
		light.ObservationRevision = 7;
		light.Identity = "actor:Engine.Light:Light155#0";
		light.ClassName = "Engine.Light";
		light.Location = { 160.0, 0.0, 0.0 };
		light.Reachable = true;
		observation.Targets.push_back(light);

		TargetSnapshot item;
		item.ObservationRevision = 7;
		item.Identity = "actor:DeusEx.Ammo10mm:Ammo10mm0#0";
		item.ClassName = "DeusEx.Ammo10mm";
		item.Location = { 160.0, 0.0, 0.0 };
		item.Reachable = true;
		item.Acquirable = true;
		observation.Targets.push_back(item);

		TargetSnapshot switchTarget;
		switchTarget.ObservationRevision = 7;
		switchTarget.Identity = "actor:DeusEx.Switch1:Switch1#0";
		switchTarget.ClassName = "DeusEx.Switch1";
		switchTarget.Location = { 20.0, 0.0, 0.0 };
		switchTarget.Reachable = true;
		switchTarget.Interactable = true;
		observation.Targets.push_back(switchTarget);
		return observation;
	}

	void CheckStatus(const std::string& raw,
		const ExpectedShadowReplayBinding& binding,
		ShadowReplayResultLoadStatus expected,
		const std::string& message)
	{
		const ShadowReplayResultLoad loaded = LoadShadowReplayResult(raw, binding);
		Check(loaded.Status == expected, message +
			(loaded.Error.empty() ? std::string() : ": " + loaded.Error));
	}

	std::string ReplaceOnce(std::string text, const std::string& from,
		const std::string& to)
	{
		const size_t offset = text.find(from);
		if (offset == std::string::npos)
			throw std::runtime_error("test replacement source was not found");
		text.replace(offset, from.size(), to);
		return text;
	}

	void TestPositiveWalkThroughNativeFixture()
	{
		const ExpectedShadowReplayBinding binding = Binding();
		const ShadowReplayResultLoad loaded =
			LoadShadowReplayResult(ResultFor(binding).dump(), binding);
		Check(loaded && loaded.Candidate && !loaded.Candidate->DispatchAuthorized &&
			loaded.Candidate->Kind == CommandKind::WalkToActor &&
			loaded.Candidate->ObservationRevision == 7 &&
			loaded.Candidate->Target &&
			loaded.Candidate->Target->Identity == "actor:Engine.Light:Light155#0" &&
			loaded.Candidate->Target->ExpectedClass == "Engine.Light" &&
			loaded.Candidate->ArrivalRadius == ShadowWalkArrivalRadius,
			"validated walk result did not produce the exact non-dispatchable candidate");
		if (!loaded.Candidate)
			return;

		const ShadowReplayFixtureRun first = RunShadowReplayFixture(
			*loaded.Candidate, Observation(), "loaded-shadow-walk", 100);
		const ShadowReplayFixtureRun second = RunShadowReplayFixture(
			*loaded.Candidate, Observation(), "loaded-shadow-walk", 100);
		Check(first.Projection && first.Projection.Command && first.Result &&
			first.Result->State == CommandState::Succeeded && first.Result->Tick == 41 &&
			!first.SyntheticInputActive,
			"loaded walk candidate did not complete through the native fixture");
		Check(second.Projection && second.Projection.Command && second.Result &&
			CommandDigest(*first.Projection.Command) ==
				CommandDigest(*second.Projection.Command) &&
			first.TelemetryJson == second.TelemetryJson &&
			CommandResultJson(*first.Result) == CommandResultJson(*second.Result) &&
			std::abs(first.FinalPosition.X - second.FinalPosition.X) < 0.0001 &&
			!second.SyntheticInputActive,
			"loaded walk replay is not byte-stable or did not release synthetic input");
	}

	void TestNoActionAndOtherValidShapes()
	{
		ExpectedShadowReplayBinding actual;
		actual.FixtureId = "deus-ex-trainingfinal-start-none-v1";
		actual.CaptureId = "deus-ex-trainingfinal-start";
		actual.FixtureSha256 =
			"593103ac92517085808077c42266ce5d0c2ea088ba77e5f16e815a1aa024fbd0";
		actual.ObservationSha256 =
			"67c23365aa84eea81dab07f8f76c69f67e75ff03703fc8a769567fa82d5151e9";
		actual.ReportSha256 =
			"6f4f69282b92cefc3f32ca476edb2e06e9aad37ad80e6cbbddff434b81cb5b6b";
		actual.ModelTag = "qwen3.5:9b";
		actual.ModelDigest =
			"6488c96fa5faab64bb65cbd30d4289e20e6130ef535a93ef9a49f42eda893ea7";
		const ShadowReplayResultLoad none = LoadShadowReplayResult(
			ResultFor(actual, "none", {}, {}, 0).dump() + "\n", actual);
		Check(none.Status == ShadowReplayResultLoadStatus::NoAction &&
			!none.Candidate && !static_cast<bool>(none),
			"saved Qwen 3.5 shaped result did not remain a no-action outcome");

		actual.ReportSha256 =
			"df95adead920cb7b6ef23cd33df1c1788e21504126555eda5d931146c8484191";
		actual.ModelTag = "qwen3.6:27b";
		actual.ModelDigest =
			"a50eda8ed977ab48a12431878896b27ffd5cef552c17af3317d9623b939a7f1e";
		const ShadowReplayResultLoad none36 = LoadShadowReplayResult(
			ResultFor(actual, "none", {}, {}, 0).dump() + "\n", actual);
		Check(none36.Status == ShadowReplayResultLoadStatus::NoAction &&
			!none36.Candidate && !static_cast<bool>(none36),
			"saved Qwen 3.6 shaped result did not remain a no-action outcome");

		const ExpectedShadowReplayBinding binding = Binding();
		const ShadowReplayResultLoad wait = LoadShadowReplayResult(
			ResultFor(binding, "wait", {}, {}, 5).dump(), binding);
		Check(wait && wait.Candidate && wait.Candidate->Kind == CommandKind::Wait &&
			wait.Candidate->WaitTicks == 5 && !wait.Candidate->Target &&
			!wait.Candidate->ArrivalRadius,
			"valid wait result did not preserve its bounded no-target shape");

		for (const auto& shape : {
			std::tuple{ "acquire_item", "actor:DeusEx.Ammo10mm:Ammo10mm0#0",
				"DeusEx.Ammo10mm", CommandKind::AcquireItem },
			std::tuple{ "interact", "actor:DeusEx.Switch1:Switch1#0",
				"DeusEx.Switch1", CommandKind::Interact } })
		{
			const ShadowReplayResultLoad loaded = LoadShadowReplayResult(
				ResultFor(binding, std::get<0>(shape), std::get<1>(shape),
					std::get<2>(shape)).dump(), binding);
			Check(loaded && loaded.Candidate &&
				loaded.Candidate->Kind == std::get<3>(shape) &&
				loaded.Candidate->ArrivalRadius == ShadowActorActionArrivalRadius,
				std::string("valid ") + std::get<0>(shape) +
				" result did not preserve the fixed actor-action shape");
		}
	}

	void TestJsonAndShapeRejections()
	{
		const ExpectedShadowReplayBinding binding = Binding();
		const std::string valid = ResultFor(binding).dump();
		CheckStatus({}, binding, ShadowReplayResultLoadStatus::EmptyInput,
			"empty result was accepted");
		CheckStatus(std::string(MaximumShadowReplayResultBytes + 1, 'x'), binding,
			ShadowReplayResultLoadStatus::InputTooLarge,
			"oversized result was accepted");
		CheckStatus("{", binding, ShadowReplayResultLoadStatus::InvalidJson,
			"malformed JSON was accepted");
		CheckStatus(valid + "{}", binding, ShadowReplayResultLoadStatus::InvalidJson,
			"trailing JSON was accepted");
		CheckStatus("/*comment*/" + valid, binding,
			ShadowReplayResultLoadStatus::InvalidJson,
			"comment-prefixed JSON was accepted");

		std::string duplicateTop = valid;
		duplicateTop.insert(1, "\"schema\":\"substituted\",");
		CheckStatus(duplicateTop, binding, ShadowReplayResultLoadStatus::DuplicateKey,
			"duplicate top-level key was accepted");
		const std::string duplicateModel = ReplaceOnce(valid,
			"\"model\":{", "\"model\":{\"tag\":\"substituted\",");
		CheckStatus(duplicateModel, binding, ShadowReplayResultLoadStatus::DuplicateKey,
			"duplicate nested model key was accepted");
		const std::string duplicateTarget = ReplaceOnce(valid,
			"\"target\":{", "\"target\":{\"identity\":\"substituted\",");
		CheckStatus(duplicateTarget, binding, ShadowReplayResultLoadStatus::DuplicateKey,
			"duplicate nested target key was accepted");

		std::string deep;
		for (int index = 0; index < MaximumShadowReplayResultDepth + 3; index++)
			deep += '[';
		deep += '0';
		for (int index = 0; index < MaximumShadowReplayResultDepth + 3; index++)
			deep += ']';
		CheckStatus(deep, binding, ShadowReplayResultLoadStatus::ExcessiveNesting,
			"excessively nested JSON was accepted");

		Json changed = ResultFor(binding);
		changed.erase("reason");
		CheckStatus(changed.dump(), binding, ShadowReplayResultLoadStatus::InvalidSchema,
			"missing top-level field was accepted");
		changed = ResultFor(binding);
		changed["extra"] = 1;
		CheckStatus(changed.dump(), binding, ShadowReplayResultLoadStatus::InvalidSchema,
			"extra top-level field was accepted");
		changed = ResultFor(binding);
		changed["model"]["extra"] = 1;
		CheckStatus(changed.dump(), binding, ShadowReplayResultLoadStatus::InvalidSchema,
			"extra model field was accepted");
		changed = ResultFor(binding);
		changed["proposal"]["extra"] = 1;
		CheckStatus(changed.dump(), binding, ShadowReplayResultLoadStatus::InvalidProposal,
			"extra proposal field was accepted");
		changed = ResultFor(binding);
		changed["shadow_candidate"]["extra"] = 1;
		CheckStatus(changed.dump(), binding, ShadowReplayResultLoadStatus::InvalidCandidate,
			"extra candidate field was accepted");
	}

	void TestProvenanceAndAuthorityRejections()
	{
		const ExpectedShadowReplayBinding binding = Binding();
		const std::string valid = ResultFor(binding).dump();
		auto CheckBindingMismatch = [&](ExpectedShadowReplayBinding changed,
			const std::string& message)
		{
			CheckStatus(valid, changed, ShadowReplayResultLoadStatus::ProvenanceMismatch,
				message);
		};
		ExpectedShadowReplayBinding changed = binding;
		changed.FixtureId = "other-fixture";
		CheckBindingMismatch(changed, "fixture identity substitution was accepted");
		changed = binding; changed.CaptureId = "other-capture";
		CheckBindingMismatch(changed, "capture identity substitution was accepted");
		changed = binding; changed.FixtureSha256 = std::string(64, '5');
		CheckBindingMismatch(changed, "fixture hash substitution was accepted");
		changed = binding; changed.ObservationSha256 = std::string(64, '5');
		CheckBindingMismatch(changed, "observation hash substitution was accepted");
		changed = binding; changed.ReportSha256 = std::string(64, '5');
		CheckBindingMismatch(changed, "report hash substitution was accepted");
		changed = binding; changed.ModelTag = "qwen3.6:27b";
		CheckBindingMismatch(changed, "model tag substitution was accepted");
		changed = binding; changed.ModelDigest = std::string(64, '5');
		CheckBindingMismatch(changed, "model digest substitution was accepted");

		changed = binding;
		changed.ReportSha256[0] = 'A';
		CheckStatus(valid, changed, ShadowReplayResultLoadStatus::InvalidBinding,
			"non-canonical trusted digest was accepted");
		Json result = ResultFor(binding);
		result["fixture_sha256"] = std::string(64, 'A');
		CheckStatus(result.dump(), binding, ShadowReplayResultLoadStatus::InvalidSchema,
			"non-canonical result digest was accepted");

		result = ResultFor(binding);
		result["verdict"] = "failed";
		CheckStatus(result.dump(), binding, ShadowReplayResultLoadStatus::InvalidSchema,
			"failed oracle verdict was accepted");
		result = ResultFor(binding);
		result["reason"] = "proposal did not match the curated acceptance oracle";
		CheckStatus(result.dump(), binding, ShadowReplayResultLoadStatus::InvalidSchema,
			"inconsistent oracle reason was accepted");
		result = ResultFor(binding);
		result["dispatch_authorized"] = true;
		CheckStatus(result.dump(), binding,
			ShadowReplayResultLoadStatus::AuthorizationRejected,
			"top-level dispatch authorization was accepted");
		result = ResultFor(binding);
		result["controls_live_player"] = true;
		CheckStatus(result.dump(), binding,
			ShadowReplayResultLoadStatus::AuthorizationRejected,
			"live-player control claim was accepted");
		result = ResultFor(binding);
		result["shadow_candidate"]["dispatch_authorized"] = true;
		CheckStatus(result.dump(), binding,
			ShadowReplayResultLoadStatus::AuthorizationRejected,
			"candidate dispatch authorization was accepted");
		result = ResultFor(binding);
		result["dispatch_authorized"] = 0;
		CheckStatus(result.dump(), binding, ShadowReplayResultLoadStatus::InvalidSchema,
			"numeric authority flag was accepted as false");
	}

	void TestProposalAndCandidateRejections()
	{
		const ExpectedShadowReplayBinding binding = Binding();
		Json result = ResultFor(binding);
		result["proposal"]["action"] = "walk_to_point";
		result["shadow_candidate"]["kind"] = "walk_to_point";
		CheckStatus(result.dump(), binding, ShadowReplayResultLoadStatus::InvalidProposal,
			"unsupported walk-to-point proposal was accepted");
		result = ResultFor(binding);
		result["proposal"]["action"] = "abort";
		result["shadow_candidate"]["kind"] = "abort";
		CheckStatus(result.dump(), binding, ShadowReplayResultLoadStatus::InvalidProposal,
			"shadow abort proposal was accepted");

		for (const Json& invalidWait : { Json(-1), Json(0.5), Json("0"), Json(false),
			Json(MaximumShadowReplayWaitTicks + 1) })
		{
			result = ResultFor(binding);
			result["proposal"]["wait_ticks"] = invalidWait;
			CheckStatus(result.dump(), binding, ShadowReplayResultLoadStatus::InvalidProposal,
				"invalid proposal wait type or bound was accepted");
		}
		result = ResultFor(binding);
		result["shadow_candidate"] = nullptr;
		CheckStatus(result.dump(), binding, ShadowReplayResultLoadStatus::InvalidCandidate,
			"positive proposal without a candidate was accepted");
		result = ResultFor(binding, "none", {}, {}, 0);
		result["shadow_candidate"] = ResultFor(binding)["shadow_candidate"];
		CheckStatus(result.dump(), binding, ShadowReplayResultLoadStatus::InvalidProposal,
			"none proposal with a candidate was accepted");
		result = ResultFor(binding);
		result["shadow_candidate"]["kind"] = "interact";
		CheckStatus(result.dump(), binding, ShadowReplayResultLoadStatus::InvalidCandidate,
			"proposal/candidate kind substitution was accepted");
		result = ResultFor(binding);
		result["shadow_candidate"]["target"]["identity"] = "actor:other";
		CheckStatus(result.dump(), binding, ShadowReplayResultLoadStatus::InvalidCandidate,
			"proposal/candidate target substitution was accepted");
		result = ResultFor(binding);
		result["shadow_candidate"]["target"]["expected_class"] = "";
		CheckStatus(result.dump(), binding, ShadowReplayResultLoadStatus::InvalidCandidate,
			"empty candidate class binding was accepted");
		result = ResultFor(binding);
		result["shadow_candidate"]["target"]["extra"] = 1;
		CheckStatus(result.dump(), binding, ShadowReplayResultLoadStatus::InvalidCandidate,
			"extra target field was accepted");

		for (const Json& revision : { Json("0"), Json("-1"), Json("1x"),
			Json("18446744073709551616"), Json(7) })
		{
			result = ResultFor(binding);
			result["shadow_candidate"]["observation_revision"] = revision;
			CheckStatus(result.dump(), binding, ShadowReplayResultLoadStatus::InvalidCandidate,
				"invalid candidate observation revision was accepted");
		}
		for (const Json& radius : { Json(nullptr), Json(48), Json(40.0), Json("40") })
		{
			result = ResultFor(binding);
			result["shadow_candidate"]["arrival_radius"] = radius;
			CheckStatus(result.dump(), binding, ShadowReplayResultLoadStatus::InvalidCandidate,
				"invalid walk radius was accepted");
		}
		const std::string hugeRadius = ReplaceOnce(ResultFor(binding).dump(),
			"\"arrival_radius\":40", "\"arrival_radius\":1e999");
		CheckStatus(hugeRadius, binding, ShadowReplayResultLoadStatus::InvalidJson,
			"non-finite radius encoding was accepted");

		result = ResultFor(binding, "wait", {}, {}, 5);
		result["shadow_candidate"]["target"] = {
			{ "identity", "actor:Engine.Light:Light155#0" },
			{ "expected_class", "Engine.Light" } };
		CheckStatus(result.dump(), binding, ShadowReplayResultLoadStatus::InvalidCandidate,
			"wait candidate with an actor target was accepted");
		result = ResultFor(binding, "wait", {}, {}, 5);
		result["shadow_candidate"]["wait_ticks"] = 6;
		CheckStatus(result.dump(), binding, ShadowReplayResultLoadStatus::InvalidCandidate,
			"wait candidate/proposal duration mismatch was accepted");
	}

	void TestPostLoadAdapterGates()
	{
		const ExpectedShadowReplayBinding binding = Binding();
		const ShadowReplayResultLoad loaded =
			LoadShadowReplayResult(ResultFor(binding).dump(), binding);
		if (!loaded.Candidate)
		{
			Check(false, "post-load adapter gates have no loaded candidate");
			return;
		}
		ObservationSnapshot stale = Observation();
		stale.Revision = 8;
		for (TargetSnapshot& target : stale.Targets)
			target.ObservationRevision = 8;
		Check(ProjectShadowReplayCandidate(*loaded.Candidate, stale,
			"loaded-stale", 100).Status == ShadowReplayStatus::StaleObservation,
			"native adapter accepted a loaded stale candidate");

		ObservationSnapshot classMismatch = Observation();
		classMismatch.Targets[0].ClassName = "Engine.Trigger";
		const ShadowReplayProjection mismatch = ProjectShadowReplayCandidate(
			*loaded.Candidate, classMismatch, "loaded-class-mismatch", 100);
		Check(mismatch.Status == ShadowReplayStatus::TargetRejected &&
			mismatch.TargetStatus == TargetResolutionStatus::ClassMismatch,
			"native adapter accepted a loaded class substitution");

		ObservationSnapshot unreachable = Observation();
		unreachable.Targets[0].Reachable = false;
		const ShadowReplayProjection rejected = ProjectShadowReplayCandidate(
			*loaded.Candidate, unreachable, "loaded-unreachable", 100);
		Check(rejected.Status == ShadowReplayStatus::TargetRejected &&
			rejected.TargetStatus == TargetResolutionStatus::Unreachable,
			"native adapter accepted a loaded unreachable target");
	}
}

int main()
{
	TestPositiveWalkThroughNativeFixture();
	TestNoActionAndOtherValidShapes();
	TestJsonAndShapeRejections();
	TestProvenanceAndAuthorityRejections();
	TestProposalAndCandidateRejections();
	TestPostLoadAdapterGates();
	if (Failures == 0)
		std::cout << "Shadow replay result loader tests passed\n";
	return Failures == 0 ? 0 : 1;
}
