#pragma once

#include "AutomationProtocol.h"

#include <cstdint>
#include <string>

namespace Automation
{
	struct DeusExSightProbeActor
	{
		std::string Identity;
		std::string ClassName;
		WorldPoint Location;
	};

	struct DeusExSightProbeRecord
	{
		std::string ConfigIdentity;
		uint64_t ObservationRevision = 0;
		uint64_t Tick = 0;
		DeusExSightProbeActor Observer;
		WorldPoint ObserverEyeLocation;
		double ViewPitchRadians = 0.0;
		double ViewYawRadians = 0.0;
		double ViewRollRadians = 0.0;
		double AddedViewPitchRadians = 0.0;
		double AddedViewYawRadians = 0.0;
		double AddedViewRollRadians = 0.0;
		double HorizontalFovDegrees = 0.0;
		double AspectRatio = 0.0;
		double MinimumAngularSize = 0.0;
		double VisibilityThreshold = 0.0;
		DeusExSightProbeActor Target;
		bool TargetDetectable = false;
		double TargetCollisionRadius = 0.0;
		double TargetCollisionHeight = 0.0;
		WorldPoint LineOfSightPrimary;
		WorldPoint LineOfSightTop;
		WorldPoint LineOfSightBottom;
		bool PrimaryVisible = false;
		bool TopTested = false;
		bool TopVisible = false;
		bool BottomTested = false;
		bool BottomVisible = false;
		uint64_t LineOfSightTraceCount = 0;
		uint64_t CylinderLineOfSightTraceCount = 0;
		double ScalarResult = 0.0;
		double DirectionResult = 0.0;
		double LineOfSightResult = 0.0;
		double CylinderLineOfSightResult = 0.0;
	};

	ValidationResult ValidateDeusExSightProbeRecord(const DeusExSightProbeRecord& record);
	std::string DeusExSightProbeJson(const DeusExSightProbeRecord& record);
	std::string DeusExSightProbeDigest(const DeusExSightProbeRecord& record);
}
