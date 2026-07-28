#pragma once

#include "Automation/ShadowReplayAdapter.h"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace Automation::TestFixtures
{
	static constexpr size_t MaximumShadowReplayResultBytes = 16 * 1024;
	static constexpr int MaximumShadowReplayResultDepth = 16;

	struct ExpectedShadowReplayBinding
	{
		std::string FixtureId;
		std::string CaptureId;
		std::string FixtureSha256;
		std::string ObservationSha256;
		std::string ReportSha256;
		std::string ModelTag;
		std::string ModelDigest;
	};

	enum class ShadowReplayResultLoadStatus
	{
		Ready,
		NoAction,
		EmptyInput,
		InputTooLarge,
		InvalidJson,
		DuplicateKey,
		ExcessiveNesting,
		InvalidBinding,
		InvalidSchema,
		ProvenanceMismatch,
		AuthorizationRejected,
		InvalidProposal,
		InvalidCandidate
	};

	struct ShadowReplayResultLoad
	{
		ShadowReplayResultLoadStatus Status =
			ShadowReplayResultLoadStatus::InvalidJson;
		std::optional<ShadowReplayCandidate> Candidate;
		std::string Error;

		explicit operator bool() const
		{
			return Status == ShadowReplayResultLoadStatus::Ready &&
				Candidate.has_value();
		}
	};

	ShadowReplayResultLoad LoadShadowReplayResult(
		std::string_view rawJson,
		const ExpectedShadowReplayBinding& expected);
}
