#include "DeusExSightProbe.h"

#include <cmath>
#include <iomanip>
#include <locale>
#include <sstream>
#include <stdexcept>

namespace Automation
{
	namespace
	{
		bool IsBoundedToken(const std::string& value, size_t maximumBytes)
		{
			if (value.empty() || value.size() > maximumBytes)
				return false;
			for (unsigned char character : value)
			{
				if (character < 0x21 || character > 0x7e)
					return false;
			}
			return true;
		}

		bool IsFinitePoint(const WorldPoint& point)
		{
			return std::isfinite(point.X) && std::isfinite(point.Y) &&
				std::isfinite(point.Z) &&
				std::abs(point.X) <= MaximumCoordinateMagnitude &&
				std::abs(point.Y) <= MaximumCoordinateMagnitude &&
				std::abs(point.Z) <= MaximumCoordinateMagnitude;
		}

		bool IsUnitResult(double value)
		{
			return std::isfinite(value) && value >= 0.0 && value <= 1.0;
		}

		std::string EscapeJson(const std::string& value)
		{
			std::ostringstream out;
			out.imbue(std::locale::classic());
			for (unsigned char character : value)
			{
				switch (character)
				{
				case '"': out << "\\\""; break;
				case '\\': out << "\\\\"; break;
				case '\b': out << "\\b"; break;
				case '\f': out << "\\f"; break;
				case '\n': out << "\\n"; break;
				case '\r': out << "\\r"; break;
				case '\t': out << "\\t"; break;
				default:
					if (character < 0x20)
						out << "\\u" << std::hex << std::setw(4) <<
							std::setfill('0') << static_cast<int>(character) << std::dec;
					else
						out << static_cast<char>(character);
				}
			}
			return out.str();
		}

		std::string JsonString(const std::string& value)
		{
			return "\"" + EscapeJson(value) + "\"";
		}

		std::string Fixed(double value)
		{
			if (!std::isfinite(value))
				throw std::invalid_argument("Deus Ex sight probe contains a non-finite number");
			if (value == 0.0)
				value = 0.0;
			std::ostringstream out;
			out.imbue(std::locale::classic());
			out << std::fixed << std::setprecision(9) << value;
			return out.str();
		}

		void WritePoint(std::ostringstream& out, const WorldPoint& point)
		{
			out << "{\"x\":" << Fixed(point.X)
				<< ",\"y\":" << Fixed(point.Y)
				<< ",\"z\":" << Fixed(point.Z) << '}';
		}

		void WriteRotation(std::ostringstream& out, double pitch, double yaw, double roll)
		{
			out << "{\"pitch\":" << Fixed(pitch)
				<< ",\"yaw\":" << Fixed(yaw)
				<< ",\"roll\":" << Fixed(roll) << '}';
		}

		void WriteActor(std::ostringstream& out, const DeusExSightProbeActor& actor)
		{
			out << "{\"identity\":" << JsonString(actor.Identity)
				<< ",\"class\":" << JsonString(actor.ClassName)
				<< ",\"location\":";
			WritePoint(out, actor.Location);
			out << '}';
		}
	}

	ValidationResult ValidateDeusExSightProbeRecord(const DeusExSightProbeRecord& record)
	{
		if (!IsBoundedToken(record.ConfigIdentity, MaximumTargetIdentityBytes) ||
			record.ObservationRevision == 0)
			return { ValidationStatus::InvalidResult,
				"sight probe provenance is missing or unbounded" };
		if (!IsBoundedToken(record.Observer.Identity, MaximumTargetIdentityBytes) ||
			!IsBoundedToken(record.Observer.ClassName, MaximumClassNameBytes) ||
			!IsFinitePoint(record.Observer.Location) ||
			!IsFinitePoint(record.ObserverEyeLocation))
			return { ValidationStatus::InvalidResult,
				"sight probe observer is invalid or unbounded" };
		if (!IsBoundedToken(record.Target.Identity, MaximumTargetIdentityBytes) ||
			!IsBoundedToken(record.Target.ClassName, MaximumClassNameBytes) ||
			!IsFinitePoint(record.Target.Location))
			return { ValidationStatus::InvalidResult,
				"sight probe target is invalid or unbounded" };
		if (record.Target.Identity == record.Observer.Identity)
			return { ValidationStatus::InvalidResult,
				"sight probe target must differ from the observer" };
		if (!std::isfinite(record.ViewPitchRadians) ||
			!std::isfinite(record.ViewYawRadians) ||
			!std::isfinite(record.ViewRollRadians) ||
			!std::isfinite(record.AddedViewPitchRadians) ||
			!std::isfinite(record.AddedViewYawRadians) ||
			!std::isfinite(record.AddedViewRollRadians) ||
			!std::isfinite(record.HorizontalFovDegrees) ||
			!std::isfinite(record.AspectRatio) ||
			!std::isfinite(record.MinimumAngularSize) ||
			!std::isfinite(record.VisibilityThreshold) ||
			!std::isfinite(record.TargetCollisionRadius) ||
			!std::isfinite(record.TargetCollisionHeight) ||
			record.TargetCollisionRadius < 0.0 || record.TargetCollisionHeight < 0.0 ||
			record.TargetCollisionRadius > MaximumCoordinateMagnitude ||
			record.TargetCollisionHeight > MaximumCoordinateMagnitude)
			return { ValidationStatus::InvalidResult,
				"sight probe runtime inputs are non-finite" };
		if (!IsFinitePoint(record.LineOfSightPrimary) ||
			!IsFinitePoint(record.LineOfSightTop) ||
			!IsFinitePoint(record.LineOfSightBottom))
			return { ValidationStatus::InvalidResult,
				"sight probe trace endpoints are invalid or unbounded" };
		if (!IsUnitResult(record.ScalarResult) || !IsUnitResult(record.DirectionResult) ||
			!IsUnitResult(record.LineOfSightResult) ||
			!IsUnitResult(record.CylinderLineOfSightResult))
			return { ValidationStatus::InvalidResult,
				"sight probe results are outside the unit interval" };
		const bool scalarPositive = record.ScalarResult > 0.0;
		if ((!scalarPositive && (record.LineOfSightTraceCount != 0 ||
			record.CylinderLineOfSightTraceCount != 0 || record.PrimaryVisible ||
			record.TopTested || record.TopVisible || record.BottomTested ||
			record.BottomVisible)) ||
			(scalarPositive && (record.LineOfSightTraceCount != 1 ||
			record.CylinderLineOfSightTraceCount !=
				static_cast<uint64_t>(1 + (record.TopTested ? 1 : 0) +
					(record.BottomTested ? 1 : 0)) ||
			record.TopTested == record.PrimaryVisible ||
			record.BottomTested != (record.TopTested && !record.TopVisible) ||
			(!record.TopTested && record.TopVisible) ||
			(!record.BottomTested && record.BottomVisible))))
			return { ValidationStatus::InvalidResult,
				"sight probe LOS trace order or count is inconsistent" };
		const double expectedLineOfSight = record.PrimaryVisible ?
			record.ScalarResult : 0.0;
		const double expectedCylinderLineOfSight =
			(record.PrimaryVisible || record.TopVisible || record.BottomVisible) ?
			record.ScalarResult : 0.0;
		if (record.LineOfSightResult != expectedLineOfSight ||
			record.CylinderLineOfSightResult != expectedCylinderLineOfSight)
			return { ValidationStatus::InvalidResult,
				"sight probe LOS results disagree with recorded traces" };
		return {};
	}

	std::string DeusExSightProbeJson(const DeusExSightProbeRecord& record)
	{
		const ValidationResult validation = ValidateDeusExSightProbeRecord(record);
		if (!validation)
			throw std::invalid_argument("invalid Deus Ex sight probe: " + validation.Error);

		std::ostringstream out;
		out.imbue(std::locale::classic());
		out << "{\n"
			<< "  \"schema\": \"surreal-deus-ex-sight-probe-v2\",\n"
			<< "  \"authority\": {\"dispatch_authorized\":false,\"gameplay_property_mutation\":false,\"collision_bookkeeping_mutation\":true},\n"
			<< "  \"config_identity\": " << JsonString(record.ConfigIdentity) << ",\n"
			<< "  \"observation_revision\": \"" << record.ObservationRevision << "\",\n"
			<< "  \"tick\": \"" << record.Tick << "\",\n"
			<< "  \"observer\": ";
		WriteActor(out, record.Observer);
		out << ",\n  \"observer_eye_location\": ";
		WritePoint(out, record.ObserverEyeLocation);
		out << ",\n  \"view_rotation_radians\": ";
		WriteRotation(out, record.ViewPitchRadians, record.ViewYawRadians,
			record.ViewRollRadians);
		out << ",\n  \"added_view_rotation_radians\": ";
		WriteRotation(out, record.AddedViewPitchRadians, record.AddedViewYawRadians,
			record.AddedViewRollRadians);
		out << ",\n  \"sight_parameters\": {"
			<< "\"horizontal_fov_degrees\":" << Fixed(record.HorizontalFovDegrees)
			<< ",\"aspect_ratio\":" << Fixed(record.AspectRatio)
			<< ",\"minimum_angular_size\":" << Fixed(record.MinimumAngularSize)
			<< ",\"visibility_threshold\":" << Fixed(record.VisibilityThreshold) << "},\n"
			<< "  \"target\": ";
		WriteActor(out, record.Target);
		out << ",\n  \"target_detectable\": " <<
			(record.TargetDetectable ? "true" : "false")
			<< ",\n  \"target_collision\": {\"radius\":" <<
			Fixed(record.TargetCollisionRadius) << ",\"height\":" <<
			Fixed(record.TargetCollisionHeight) << "},\n"
			<< "  \"los_traces\": {\n"
			<< "    \"primary\": {\"endpoint\":";
		WritePoint(out, record.LineOfSightPrimary);
		out << ",\"tested\":" << (record.LineOfSightTraceCount != 0 ? "true" : "false")
			<< ",\"visible\":" << (record.PrimaryVisible ? "true" : "false") << "},\n"
			<< "    \"top\": {\"endpoint\":";
		WritePoint(out, record.LineOfSightTop);
		out << ",\"tested\":" << (record.TopTested ? "true" : "false")
			<< ",\"visible\":" << (record.TopVisible ? "true" : "false") << "},\n"
			<< "    \"bottom\": {\"endpoint\":";
		WritePoint(out, record.LineOfSightBottom);
		out << ",\"tested\":" << (record.BottomTested ? "true" : "false")
			<< ",\"visible\":" << (record.BottomVisible ? "true" : "false") << "},\n"
			<< "    \"non_cylinder_trace_count\":\"" << record.LineOfSightTraceCount << "\",\n"
			<< "    \"cylinder_trace_count\":\"" << record.CylinderLineOfSightTraceCount << "\",\n"
			<< "    \"collision_contract\": {\"trace_actors\":false,\"trace_world\":true,\"visibility_only\":false,\"radius\":0.000000000,\"tmin\":0.010000000,\"endpoint_margin\":1.000000000}\n"
			<< "  },\n"
			<< "  \"evaluations\": {\n"
			<< "    \"scalar\": {\"visibility\":1.000000,\"check_visibility\":false,\"check_direction\":false,\"check_cylinder\":false,\"check_los\":false,\"result\":" <<
			Fixed(record.ScalarResult) << "},\n"
			<< "    \"direction\": {\"visibility\":1.000000,\"check_visibility\":false,\"check_direction\":true,\"check_cylinder\":false,\"check_los\":false,\"result\":" <<
			Fixed(record.DirectionResult) << "},\n"
			<< "    \"los\": {\"visibility\":1.000000,\"check_visibility\":false,\"check_direction\":false,\"check_cylinder\":false,\"check_los\":true,\"result\":" <<
			Fixed(record.LineOfSightResult) << "},\n"
			<< "    \"cylinder_los\": {\"visibility\":1.000000,\"check_visibility\":false,\"check_direction\":false,\"check_cylinder\":true,\"check_los\":true,\"result\":" <<
			Fixed(record.CylinderLineOfSightResult) << "}\n"
			<< "  }\n"
			<< "}\n";
		return out.str();
	}

	std::string DeusExSightProbeDigest(const DeusExSightProbeRecord& record)
	{
		uint64_t hash = 14695981039346656037ULL;
		for (unsigned char character : DeusExSightProbeJson(record))
		{
			hash ^= character;
			hash *= 1099511628211ULL;
		}
		std::ostringstream out;
		out.imbue(std::locale::classic());
		out << "fnv1a64:" << std::hex << std::setw(16) << std::setfill('0') << hash;
		return out.str();
	}
}
