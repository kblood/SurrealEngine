#pragma once

#include "AutomationProtocol.h"

#include <cstdint>
#include <optional>
#include <string>

namespace Automation
{
	enum class AutomationCapturePhase
	{
		PostSimulation,
		PreAction,
		PreStockInteraction,
		PrePickup
	};

	const char* AutomationCapturePhaseName(AutomationCapturePhase phase);

	struct AutomationCaptureRequest
	{
		uint64_t Tick = 0;
		std::string SessionId;
		std::string SourceRevision;
		bool SourceDirty = false;
		AutomationCapturePhase Phase = AutomationCapturePhase::PostSimulation;
	};

	class PlayerAutomationRunConfig
	{
	public:
		static PlayerAutomationRunConfig Parse(
			std::string url,
			std::string outputDirectory,
			std::string targetX,
			std::string targetY,
			std::string targetZ,
			std::string arrivalRadius,
			std::string seed,
			std::string maxTicks,
			std::string fixedDelta,
			std::string action = {},
			std::string waitTicks = {},
			std::string targetIdentity = {},
			std::string targetClass = {},
			std::string abortAtTick = {},
			std::string followupTargetIdentity = {},
			std::string captureTick = {},
			std::string captureSessionId = {},
			std::string captureSourceRevision = {},
			std::string captureSourceDirty = {},
			std::string capturePhase = {});

		const std::string& GetURL() const { return URL; }
		const std::string& GetOutputDirectory() const { return OutputDirectory; }
		uint64_t GetSeed() const { return Seed; }
		uint64_t GetMaxTicks() const { return MaxTicks; }
		float GetFixedDelta() const { return FixedDelta; }
		const AutomationCommand& GetCommand() const { return Command; }
		const std::optional<AutomationCommand>& GetAbortCommand() const { return AbortCommand; }
		const std::optional<AutomationCommand>& GetFollowupCommandTemplate() const
		{
			return FollowupCommandTemplate;
		}
		bool HasFollowupCommand() const { return FollowupCommandTemplate.has_value(); }
		AutomationCommand MaterializeFollowupCommand(uint64_t issuedTick,
			uint64_t observationRevision) const;
		const std::optional<TargetSelector>& GetSightProbeTarget() const { return SightProbeTarget; }
		bool IsSightProbe() const { return SightProbeTarget.has_value(); }
		const std::optional<AutomationCaptureRequest>& GetCaptureRequest() const
		{
			return CaptureRequest;
		}
		bool HasCaptureRequest() const { return CaptureRequest.has_value(); }
		std::string ConfigIdentity() const;
		std::string ManifestJson() const;

	private:
		PlayerAutomationRunConfig(std::string url, std::string outputDirectory,
			uint64_t seed, uint64_t maxTicks, float fixedDelta,
			AutomationCommand command, std::optional<AutomationCommand> abortCommand,
			std::optional<AutomationCommand> followupCommandTemplate,
			std::optional<TargetSelector> sightProbeTarget,
			std::optional<AutomationCaptureRequest> captureRequest);

		std::string URL;
		std::string OutputDirectory;
		uint64_t Seed = 0;
		uint64_t MaxTicks = 0;
		float FixedDelta = 0.0f;
		AutomationCommand Command;
		std::optional<AutomationCommand> AbortCommand;
		std::optional<AutomationCommand> FollowupCommandTemplate;
		std::optional<TargetSelector> SightProbeTarget;
		std::optional<AutomationCaptureRequest> CaptureRequest;
	};
}
