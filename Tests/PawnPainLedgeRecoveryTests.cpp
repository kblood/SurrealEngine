#include "UObject/PawnPainLedgeRecovery.h"

#include <cmath>
#include <iostream>
#include <limits>

namespace
{
	int Failures = 0;

	void Check(bool condition, const char* message)
	{
		if (!condition)
		{
			std::cerr << "FAIL: " << message << '\n';
			Failures++;
		}
	}

	bool Near(float left, float right)
	{
		return std::abs(left - right) < 0.0001f;
	}
}

int main()
{
	using namespace PawnMovement;

	const PainLedgeVetoRecord first = RecordPainLedgeVeto({}, vec3(10.0f, 20.0f, 30.0f),
		vec2(4.0f, 0.0f), 0.5f, 96.0f, 0.5f);
	Check(first.State.Active && !first.Repeat, "first veto starts non-repeat recovery memory");
	Check(Near(first.State.UnsafeDirection.x, 1.0f) && Near(first.State.RemainingSeconds, 0.5f),
		"veto memory normalizes direction and preserves duration");

	const PainLedgeVetoRecord repeat = RecordPainLedgeVeto(first.State, vec3(30.0f, 20.0f, 30.0f),
		vec2(1.0f, 0.1f), 0.5f, 96.0f, 0.5f);
	Check(repeat.Repeat, "nearby aligned veto is counted as a repeat");
	const PainLedgeVetoRecord distinct = RecordPainLedgeVeto(first.State, vec3(10.0f, 20.0f, 30.0f),
		vec2(-1.0f, 0.0f), 0.5f, 96.0f, 0.5f);
	Check(!distinct.Repeat, "opposite veto direction is not counted as a repeat");

	PainLedgeRecoveryAdvance advance = AdvancePainLedgeRecovery(first.State,
		vec3(10.0f, 20.0f, 30.0f), 0.2f, 96.0f, true);
	Check(advance.State.Active && Near(advance.State.RemainingSeconds, 0.3f),
		"active memory advances by elapsed simulation time");
	advance = AdvancePainLedgeRecovery(advance.State, vec3(10.0f, 20.0f, 30.0f), 0.3f, 96.0f, true);
	Check(!advance.State.Active, "memory clears at its expiry");

	PainLedgeRecoveryState attempted = first.State;
	attempted.RecoveryAttempted = true;
	advance = AdvancePainLedgeRecovery(attempted, vec3(107.0f, 20.0f, 30.0f), 0.01f, 96.0f, true);
	Check(!advance.State.Active && advance.Escaped, "leaving the local radius after steering counts an escape");
	advance = AdvancePainLedgeRecovery(attempted, attempted.Origin, 0.01f, 96.0f, false);
	Check(!advance.State.Active && !advance.Escaped, "invalid movement context clears without an escape");

	Check(EvaluatePainLedgeRecoveryRequest(first.State, vec2(1.0f, 0.0f), 0.5f)
		== PainLedgeRecoveryRequest::Recover, "aligned request activates recovery");
	Check(EvaluatePainLedgeRecoveryRequest(first.State, vec2(0.0f, 1.0f), 0.5f)
		== PainLedgeRecoveryRequest::Clear, "nonaligned request clears local edge memory");
	Check(ShouldRejectPainLedgeInventoryCommand(
		first.State, true, vec2(1.0f, 0.0f), 0.5f),
		"an aligned direct inventory command is rejected during recent recovery");
	Check(!ShouldRejectPainLedgeInventoryCommand(
		first.State, true, vec2(-1.0f, 0.0f), 0.5f),
		"an opposite direct inventory command preserves stock behavior");
	Check(!ShouldRejectPainLedgeInventoryCommand(
		first.State, true, vec2(0.0f, 1.0f), 0.5f),
		"a safe nonaligned direct inventory command preserves stock behavior");
	Check(!ShouldRejectPainLedgeInventoryCommand(
		first.State, false, vec2(1.0f, 0.0f), 0.5f),
		"a graph NavigationPoint command is never rejected by the direct-inventory guard");
	Check(!ShouldRejectPainLedgeInventoryCommand(
		{}, true, vec2(1.0f, 0.0f), 0.5f),
		"without active veto memory direct inventory behavior remains stock");
	PainLedgeRecoveryState expired = first.State;
	expired.RemainingSeconds = 0.0f;
	Check(!ShouldRejectPainLedgeInventoryCommand(
		expired, true, vec2(1.0f, 0.0f), 0.5f),
		"an expired veto preserves stock direct inventory behavior");
	expired.RemainingSeconds = std::numeric_limits<float>::quiet_NaN();
	Check(!ShouldRejectPainLedgeInventoryCommand(
		expired, true, vec2(1.0f, 0.0f), 0.5f),
		"invalid veto lifetime fails open");

	const auto directions = PainLedgeRecoveryCandidateDirections(vec2(1.0f, 0.0f));
	Check(Near(directions[0].x, -1.0f) && Near(directions[0].y, 0.0f),
		"reverse is the first recovery candidate");
	Check(Near(directions[1].x, 0.0f) && Near(directions[1].y, 1.0f),
		"left is the second recovery candidate");
	Check(Near(directions[2].x, 0.0f) && Near(directions[2].y, -1.0f),
		"right is the third recovery candidate");
	std::array<PainLedgeRecoveryCandidateProbe, 3> probes = {{
		{ true, false, true },
		{ true, true, true },
		{ true, true, true }
	}};
	Check(SelectPainLedgeRecoveryCandidate(probes) == 1,
		"candidate selection skips unsafe reverse and deterministically selects left");
	probes[1].WalkableSupport = false;
	Check(SelectPainLedgeRecoveryCandidate(probes) == 2,
		"candidate selection falls back from left to right");
	probes[2].SweepClear = false;
	Check(SelectPainLedgeRecoveryCandidate(probes) == -1,
		"candidate selection rejects incomplete safety probes");

	if (Failures == 0)
		std::cout << "All pawn pain-ledge recovery tests passed.\n";
	return Failures == 0 ? 0 : 1;
}
