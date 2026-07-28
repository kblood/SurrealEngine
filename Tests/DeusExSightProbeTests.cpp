#include "Automation/DeusExSightProbe.h"

#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

using namespace Automation;

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

	DeusExSightProbeRecord ValidRecord()
	{
		DeusExSightProbeRecord record;
		record.ConfigIdentity = "0123456789abcdef";
		record.ObservationRevision = 1;
		record.Observer = {
			"actor:DeusEx.JCDentonMale:JCDentonMale0#0",
			"DeusEx.JCDentonMale", { -10.0, 20.0, 30.0 } };
		record.ObserverEyeLocation = { -10.0, 20.0, 70.0 };
		record.ViewPitchRadians = 0.125;
		record.ViewYawRadians = -0.25;
		record.AddedViewYawRadians = 0.03125;
		record.HorizontalFovDegrees = 90.0;
		record.AspectRatio = 1.333333;
		record.MinimumAngularSize = 0.0001;
		record.VisibilityThreshold = 0.01;
		record.Target = {
			"actor:DeusEx.Soldier:Soldier0#0",
			"DeusEx.Soldier", { 100.0, 200.0, -30.0 } };
		record.TargetDetectable = true;
		record.TargetCollisionRadius = 20.0;
		record.TargetCollisionHeight = 40.0;
		record.LineOfSightPrimary = { 100.0, 200.0, 10.0 };
		record.LineOfSightTop = { 100.0, 200.0, 10.0 };
		record.LineOfSightBottom = { 100.0, 200.0, -70.0 };
		record.PrimaryVisible = false;
		record.TopTested = true;
		record.TopVisible = true;
		record.BottomTested = false;
		record.BottomVisible = false;
		record.LineOfSightTraceCount = 1;
		record.CylinderLineOfSightTraceCount = 2;
		record.ScalarResult = 0.5;
		record.DirectionResult = 0.25;
		record.LineOfSightResult = 0.0;
		record.CylinderLineOfSightResult = 0.5;
		return record;
	}

	void TestCanonicalArtifact()
	{
		const DeusExSightProbeRecord record = ValidRecord();
		const std::string first = DeusExSightProbeJson(record);
		const std::string second = DeusExSightProbeJson(record);
		Check(first == second, "repeated sight probe serialization changed");
		Check(first.find("surreal-deus-ex-sight-probe-v2") != std::string::npos,
			"artifact omitted its versioned schema");
		Check(first.find("\"dispatch_authorized\":false") != std::string::npos &&
			first.find("\"gameplay_property_mutation\":false") != std::string::npos &&
			first.find("\"collision_bookkeeping_mutation\":true") != std::string::npos,
			"artifact omitted the authority boundary");
		Check(first.find("\"check_visibility\":false") != std::string::npos &&
			first.find("\"check_los\":false") != std::string::npos &&
			first.find("\"check_direction\":false") != std::string::npos &&
			first.find("\"check_direction\":true") != std::string::npos,
			"artifact omitted the fixed partial-sight call contract");
		Check(first.find("\"result\":0.500000000") != std::string::npos &&
			first.find("\"result\":0.250000000") != std::string::npos,
			"artifact did not preserve distinct scalar and direction results");
		Check(first.find("\"primary\": {\"endpoint\":") != std::string::npos &&
			first.find("\"top\": {\"endpoint\":") != std::string::npos &&
			first.find("\"cylinder_trace_count\":\"2\"") != std::string::npos &&
			first.find("\"cylinder_los\":") != std::string::npos &&
			first.find("\"check_los\":true") != std::string::npos,
			"artifact omitted the exact LOS endpoints or evaluations");
		Check(DeusExSightProbeDigest(record) == DeusExSightProbeDigest(record) &&
			DeusExSightProbeDigest(record).starts_with("fnv1a64:"),
			"artifact digest is not stable or named");
		Check(DeusExSightProbeDigest(record) == "fnv1a64:71df29b41f0c8582",
			"canonical sight probe digest changed");
	}

	void TestValidationFailures()
	{
		DeusExSightProbeRecord record = ValidRecord();
		record.ScalarResult = std::numeric_limits<double>::quiet_NaN();
		Check(!ValidateDeusExSightProbeRecord(record),
			"non-finite scalar result was accepted");
		record = ValidRecord();
		record.DirectionResult = 1.01;
		Check(!ValidateDeusExSightProbeRecord(record),
			"out-of-range direction result was accepted");
		record = ValidRecord();
		record.Target.Identity.clear();
		Check(!ValidateDeusExSightProbeRecord(record),
			"missing target identity was accepted");
		record = ValidRecord();
		record.Target.Identity = record.Observer.Identity;
		Check(!ValidateDeusExSightProbeRecord(record),
			"observer was accepted as its own sight target");
		record = ValidRecord();
		record.TargetCollisionRadius = -1.0;
		Check(!ValidateDeusExSightProbeRecord(record),
			"negative target collision extent was accepted");
		record = ValidRecord();
		record.ObserverEyeLocation.X = std::numeric_limits<double>::infinity();
		Check(!ValidateDeusExSightProbeRecord(record),
			"non-finite eye location was accepted");
		record = ValidRecord();
		record.LineOfSightResult = record.ScalarResult;
		Check(!ValidateDeusExSightProbeRecord(record),
			"LOS result inconsistent with the recorded blocked center was accepted");
		record = ValidRecord();
		record.LineOfSightTop.Z = std::numeric_limits<double>::quiet_NaN();
		Check(!ValidateDeusExSightProbeRecord(record),
			"non-finite LOS endpoint was accepted");
	}
}

int main()
{
	TestCanonicalArtifact();
	TestValidationFailures();
	if (Failures == 0)
		std::cout << "Deus Ex sight probe tests passed\n";
	return Failures == 0 ? 0 : 1;
}
