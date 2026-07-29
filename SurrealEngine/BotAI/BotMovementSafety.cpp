#include "BotMovementSafety.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>

namespace BotAI
{
	namespace
	{
		constexpr double Epsilon = 0.000001;

		bool IsFinite(const Vector3& value)
		{
			return std::isfinite(value.X) && std::isfinite(value.Y) && std::isfinite(value.Z);
		}

		double LengthSquared(const Vector3& value)
		{
			return value.X * value.X + value.Y * value.Y + value.Z * value.Z;
		}

		Vector3 Normalized(const Vector3& value)
		{
			const double lengthSquared = LengthSquared(value);
			if (lengthSquared <= Epsilon)
				return {};
			const double inverseLength = 1.0 / std::sqrt(lengthSquared);
			return { value.X * inverseLength, value.Y * inverseLength, value.Z * inverseLength };
		}

		double Dot(const Vector3& left, const Vector3& right)
		{
			return left.X * right.X + left.Y * right.Y + left.Z * right.Z;
		}

		MovementAlternative Assess(const MovementSafetyContext& context, const MovementProbe& probe,
			const Vector3& desiredDirection, bool recovering, bool repeatRecoveryProbe)
		{
			MovementAlternative result;
			result.ProbeIndex = probe.Index;
			result.Direction = Normalized(probe.Direction);

			if (!IsFinite(probe.Direction) || LengthSquared(probe.Direction) <= Epsilon
				|| !std::isfinite(probe.ForwardClearance) || !std::isfinite(probe.LandingDrop)
				|| !std::isfinite(probe.ExpectedDamage) || probe.ForwardClearance < 0.0
				|| probe.LandingDrop < 0.0 || probe.ExpectedDamage < 0.0)
			{
				result.Status = MovementProbeStatus::Invalid;
				result.Score = -1000000.0;
				result.Reason = "probe contains invalid geometry data";
				return result;
			}

			if (probe.ForwardClearance < context.CollisionRadius)
			{
				result.Status = MovementProbeStatus::Blocked;
				result.Score = -9000.0;
				result.Reason = "forward clearance is smaller than the collision radius";
				return result;
			}
			if (probe.EntersPainZone && !context.InPainZone)
			{
				result.Status = MovementProbeStatus::PainZone;
				result.Score = -8000.0 - probe.ExpectedDamage;
				result.Reason = "probe enters a pain zone";
				return result;
			}
			if (!probe.HasLanding)
			{
				result.Status = MovementProbeStatus::UnsupportedLanding;
				result.Score = -7000.0;
				result.Reason = "probe has no supported landing";
				return result;
			}
			if (probe.LandingDrop > context.SafeDropDistance)
			{
				result.Status = MovementProbeStatus::ExcessiveDrop;
				result.Score = -6000.0 - probe.LandingDrop;
				result.Reason = "landing drop exceeds the configured safe distance";
				return result;
			}
			if (probe.RequiresJump && !probe.LandingReachable)
			{
				result.Status = MovementProbeStatus::UnsafeJump;
				result.Score = -5000.0;
				result.Reason = "jump prediction cannot reach its landing";
				return result;
			}

			const double alignment = Dot(desiredDirection, result.Direction);
			const double normalizedClearance = std::min(probe.ForwardClearance, 1024.0) / 1024.0;
			result.Status = MovementProbeStatus::Viable;
			result.Score = alignment * 100.0 + normalizedClearance * 10.0 - probe.ExpectedDamage;
			result.Reason = "clear route with a supported safe landing";

			if (context.InPainZone && !probe.EntersPainZone)
			{
				result.Score += 1000.0;
				result.Reason = "safe exit from the current pain zone";
			}
			if (recovering)
			{
				// A recovery candidate should break the failed direction, not keep
				// pushing into it. Penalizing the previous candidate gives bounded,
				// deterministic cycling without random movement.
				result.Score += (1.0 - std::abs(alignment)) * 200.0;
				if (alignment > 0.8)
					result.Score -= 150.0;
				if (repeatRecoveryProbe)
					result.Score -= 400.0;
				result.Reason = "safe bounded stuck-recovery candidate";
			}
			return result;
		}
	}

	void MovementSafetyAdvisor::Reset()
	{
		HasRecoveryProbe = false;
		LastRecoveryProbe = 0;
	}

	MovementAdvice MovementSafetyAdvisor::Evaluate(const MovementSafetyContext& context, const std::vector<MovementProbe>& probes)
	{
		MovementAdvice advice;
		if (!IsFinite(context.DesiredDirection) || !IsFinite(context.Velocity)
			|| LengthSquared(context.DesiredDirection) <= Epsilon || !std::isfinite(context.CollisionRadius)
			|| !std::isfinite(context.SafeDropDistance) || !std::isfinite(context.StuckSeconds)
			|| context.CollisionRadius <= 0.0 || context.SafeDropDistance < 0.0 || context.StuckSeconds < 0.0
			|| probes.empty() || probes.size() > MaxMovementSafetyProbes)
		{
			advice.ValidInput = false;
			advice.Reason = "movement-safety input is invalid or exceeds the bounded probe count";
			return advice;
		}

		const bool recovering = context.StuckSeconds >= 1.0;
		const Vector3 desiredDirection = Normalized(context.DesiredDirection);
		std::set<uint32_t> indices;
		advice.Alternatives.reserve(probes.size());
		for (const MovementProbe& probe : probes)
		{
			if (!indices.insert(probe.Index).second)
			{
				advice.ValidInput = false;
				advice.Alternatives.clear();
				advice.Reason = "movement probes must have unique stable indices";
				return advice;
			}
			advice.Alternatives.push_back(Assess(context, probe, desiredDirection, recovering,
				recovering && HasRecoveryProbe && probe.Index == LastRecoveryProbe));
		}

		std::sort(advice.Alternatives.begin(), advice.Alternatives.end(), [](const MovementAlternative& left, const MovementAlternative& right)
		{
			if (left.Score != right.Score)
				return left.Score > right.Score;
			return left.ProbeIndex < right.ProbeIndex;
		});

		const auto selected = std::find_if(advice.Alternatives.begin(), advice.Alternatives.end(), [](const MovementAlternative& alternative)
		{
			return alternative.Status == MovementProbeStatus::Viable;
		});
		if (selected == advice.Alternatives.end())
		{
			advice.Reason = "no probe has a clear route and safe supported landing";
			return advice;
		}

		advice.SelectedProbeIndex = selected->ProbeIndex;
		advice.Direction = selected->Direction;
		advice.Score = selected->Score;
		advice.Reason = selected->Reason;
		if (context.InPainZone)
			advice.Action = MovementAdviceAction::EscapeHazard;
		else if (recovering)
		{
			advice.Action = MovementAdviceAction::RecoverFromStuck;
			HasRecoveryProbe = true;
			LastRecoveryProbe = selected->ProbeIndex;
		}
		else if (Dot(desiredDirection, selected->Direction) >= 0.95)
			advice.Action = MovementAdviceAction::Continue;
		else
			advice.Action = MovementAdviceAction::Steer;
		return advice;
	}
}
