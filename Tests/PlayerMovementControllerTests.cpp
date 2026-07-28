#include "Automation/PlayerMovementController.h"
#include "Input/InputComposition.h"

#include <cmath>
#include <iostream>
#include <limits>
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

	void Check(const ValidationResult& result, const std::string& message)
	{
		Check(static_cast<bool>(result), message + (result.Error.empty() ? std::string() : ": " + result.Error));
	}

	bool Near(double left, double right)
	{
		return std::abs(left - right) < 0.0001;
	}

	AutomationCommand Walk(double x, double y, uint64_t deadline = 500)
	{
		AutomationCommand command;
		command.Id = "walk.fixture";
		command.Kind = CommandKind::WalkToPoint;
		command.IssuedTick = 10;
		command.DeadlineTick = deadline;
		command.Point = WorldPoint{ x, y, 0.0 };
		command.ArrivalRadius = 8.0;
		return command;
	}

	class RecordingInputTarget final : public InputCommandTarget
	{
	public:
		void InputCommand(const std::string& command, InputControlId control, float delta) override
		{
			if (command.find("aBaseY") != std::string::npos)
				Composition.SetAxis("aBaseY", control, 7000.0f * delta);
			else if (command.find("aStrafe") != std::string::npos)
				Composition.SetAxis("aStrafe", control, 7000.0f * delta);
			else if (command.find("aTurn") != std::string::npos)
				Composition.SetAxis("aTurn", control, 4096.0f * delta);
			else if (command.find("aLookUp") != std::string::npos)
				Composition.SetAxis("aLookUp", control, 4096.0f * delta);
		}

		void ReleaseInputControl(InputControlId control) override
		{
			Composition.ReleaseControl(control);
			ReleaseControlCount++;
		}

		void ReleaseInputSource(InputSourceId source) override
		{
			Composition.ReleaseSource(source);
			ReleaseSourceCount++;
		}

		InputComposition Composition;
		int ReleaseControlCount = 0;
		int ReleaseSourceCount = 0;
	};

	void TestSteering()
	{
		PlayerMovementController controller;
		AutomationCommand command = Walk(100.0, 0.0);
		Check(controller.Start(command, {}, 10, 0), "forward walk starts");
		MovementStep forward = controller.Update({ 10, { 0.0, 0.0, 0.0 }, 0.0 });
		Check(forward.Status == MovementStatus::Running && Near(forward.Forward, 1.0) && Near(forward.Turn, 0.0),
			"target ahead produces forward input");

		controller.Start(Walk(0.0, 100.0), {}, 10, 0);
		MovementStep left = controller.Update({ 10, { 0.0, 0.0, 0.0 }, 0.0 });
		Check(left.Status == MovementStatus::Running && Near(left.Forward, 0.0) && Near(left.Turn, 1.0),
			"quarter-turn target rotates before advancing");

		controller.Start(Walk(50.0, 86.6025403784), {}, 10, 0);
		MovementStep turnInPlace = controller.Update({ 10, { 0.0, 0.0, 0.0 }, 0.0 });
		Check(turnInPlace.Status == MovementStatus::Running && Near(turnInPlace.Forward, 0.0) &&
			Near(turnInPlace.Turn, 1.0),
			"large heading error turns in place instead of driving into an obstacle");

		controller.Start(Walk(0.0, -100.0), {}, 10, 0);
		MovementStep right = controller.Update({ 10, { 0.0, 0.0, 0.0 }, 0.0 });
		Check(right.Status == MovementStatus::Running && Near(right.Turn, -1.0),
			"turn direction is deterministic");
	}

	void TestArrivalTimeoutAndStuck()
	{
		PlayerMovementController controller;
		AutomationCommand command = Walk(100.0, 0.0, 20);
		controller.Start(command, {}, 10, 0);
		MovementStep arrived = controller.Update({ 11, { 95.0, 0.0, 0.0 }, 0.0 });
		Check(arrived.Status == MovementStatus::Arrived && !controller.IsActive(),
			"arrival envelope terminates movement");

		controller.Start(command, {}, 10, 0);
		MovementStep timedOut = controller.Update({ 21, { 0.0, 0.0, 0.0 }, 0.0 });
		Check(timedOut.Status == MovementStatus::TimedOut && !controller.IsActive(),
			"deadline terminates movement");

		MovementControllerConfig config;
		config.StuckTicks = 3;
		controller = PlayerMovementController(config);
		command = Walk(100.0, 0.0);
		controller.Start(command, {}, 10, 0);
		controller.Update({ 10, { 0.0, 0.0, 0.0 }, 0.0 });
		controller.Update({ 11, { 0.0, 0.0, 0.0 }, 0.0 });
		controller.Update({ 12, { 0.0, 0.0, 0.0 }, 0.0 });
		MovementStep stuck = controller.Update({ 13, { 0.0, 0.0, 0.0 }, 0.0 });
		Check(stuck.Status == MovementStatus::Stuck && !controller.IsActive(),
			"bounded no-progress window detects stuck movement");

		controller = PlayerMovementController(config);
		controller.Start(command, {}, 10, 0);
		controller.Update({ 10, { 0.0, 0.0, 0.0 }, 0.0 });
		controller.Update({ 12, { 2.0, 0.0, 0.0 }, 0.0 });
		MovementStep progressing = controller.Update({ 14, { 4.0, 0.0, 0.0 }, 0.0 });
		Check(progressing.Status == MovementStatus::Running,
			"bounded progress resets the stuck window");

		MovementStep invalid = controller.Update({ 13, { 4.0, 0.0, 0.0 }, 0.0 });
		Check(invalid.Status == MovementStatus::Invalid && !controller.IsActive(),
			"out-of-order observations fail closed");

		config.StuckTicks = 20;
		config.ObstacleProbeTicks = 2;
		config.ObstacleStrafeTicks = 3;
		controller = PlayerMovementController(config);
		controller.Start(command, {}, 10, 0);
		controller.Update({ 10, { 0.0, 0.0, 0.0 }, 0.0 });
		controller.Update({ 11, { 0.0, 0.0, 0.0 }, 0.0 });
		MovementStep recovery = controller.Update({ 12, { 0.0, 0.0, 0.0 }, 0.0 });
		Check(recovery.Status == MovementStatus::Running && Near(recovery.Strafe, 1.0) &&
			recovery.Forward <= 0.25,
			"bounded no-progress probe did not begin deterministic lateral recovery");
	}

	void TestResolvedActorContract()
	{
		AutomationCommand command;
		command.Id = "walk.nano-key";
		command.Kind = CommandKind::WalkToActor;
		command.IssuedTick = 10;
		command.DeadlineTick = 500;
		command.Target = TargetSelector{ 7, "actor:NanoKey0", "DeusEx.NanoKey" };
		command.ArrivalRadius = 48.0;

		TargetSnapshot target;
		target.ObservationRevision = 7;
		target.Identity = "actor:NanoKey0";
		target.ClassName = "DeusEx.NanoKey";
		target.Location = { 100.0, 0.0, 0.0 };
		target.Reachable = true;

		PlayerMovementController controller;
		Check(controller.Start(command, target, 10, 7), "matching resolved actor starts movement");
		Check(controller.Retarget({ 64.0, 32.0, 0.0 }, 16.0, 10),
			"bounded navigation segment was rejected");
		MovementStep routeStep = controller.Update({ 11, { 64.0, 32.0, 0.0 }, 0.0 });
		Check(routeStep.Status == MovementStatus::Arrived,
			"navigation segment did not use its replacement point and radius");
		Check(controller.Retarget({ 0.0, 0.0, 0.0 }, 16.0, 12).Status ==
			ValidationStatus::InvalidCommandState,
			"inactive movement controller accepted a route segment");
		target.Identity = "actor:Other";
		Check(controller.Start(command, target, 10, 7).Status == ValidationStatus::InvalidTarget,
			"resolved actor identity cannot be substituted");
	}

	void TestBoundedAbort()
	{
		PlayerMovementController controller;
		AutomationCommand movement = Walk(100.0, 0.0);
		Check(controller.Start(movement, {}, 10, 0),
			"movement abort fixture did not start");

		AutomationCommand abort;
		abort.Id = "abort.fixture";
		abort.Kind = CommandKind::Abort;
		abort.IssuedTick = 11;
		abort.DeadlineTick = 12;
		abort.AbortCommandId = "another-command";
		Check(!controller.Abort(abort, 11, 0) && controller.IsActive(),
			"wrong-command abort cancelled active movement");
		abort.AbortCommandId = movement.Id;
		Check(controller.Abort(abort, 11, 0) && !controller.IsActive(),
			"bounded abort did not cancel the named movement command");
		Check(!controller.Abort(abort, 11, 0),
			"inactive movement accepted a repeated abort");
	}

	void TestInputCompositionAndRelease()
	{
		RecordingInputTarget target;
		target.Composition.SetAxis("aBaseY", { InputSourceId::KeyboardMouse, 1 }, 7000.0f);
		target.Composition.SetAxis("aUp", { InputSourceId::Synthetic, 999 }, 250.0f);

		PlayerMovementInputAdapter adapter;
		MovementStep running;
		running.Status = MovementStatus::Running;
		running.Forward = 1.0;
		running.Turn = 0.5;
		adapter.Apply(running, target);
		Check(adapter.IsActive(), "movement adapter becomes active");
		Check(Near(target.Composition.GetAxisValue("aBaseY"), 14000.0),
			"synthetic movement composes with keyboard input");
		Check(Near(target.Composition.GetAxisValue("aTurn"), 2048.0),
			"synthetic turn uses the bounded UE1 automation scale");

		MovementStep arrived;
		arrived.Status = MovementStatus::Arrived;
		adapter.Apply(arrived, target);
		Check(!adapter.IsActive() && target.ReleaseControlCount == 3 && target.ReleaseSourceCount == 0,
			"terminal movement releases only its three synthetic controls");
		Check(Near(target.Composition.GetAxisValue("aBaseY"), 7000.0),
			"releasing automation preserves keyboard contribution");
		Check(Near(target.Composition.GetAxisValue("aTurn"), 0.0),
			"releasing automation clears its unique turn contribution");
		Check(Near(target.Composition.GetAxisValue("aUp"), 250.0),
			"releasing movement preserves unrelated synthetic controls");

		adapter.Release(target);
		Check(target.ReleaseControlCount == 3, "inactive release is idempotent");
	}

	void TestBoundedInteractionAim()
	{
		PlayerAimController controller;
		AimObservation observation;
		observation.EyePosition = {};
		AimStep aligned = controller.Update(observation, { 100.0, 0.0, 0.0 });
		Check(aligned.Status == AimStatus::Aligned && Near(aligned.Turn, 0.0) &&
			Near(aligned.LookUp, 0.0),
			"forward interaction target was not already aligned");

		AimStep offset = controller.Update(observation, { 0.0, 100.0, 100.0 });
		Check(offset.Status == AimStatus::Aligning && Near(offset.Turn, 1.0) &&
			Near(offset.LookUp, 1.0),
			"interaction target did not produce bounded yaw and pitch input");

		AimObservation invalid = observation;
		invalid.ViewPitchRadians = std::numeric_limits<double>::quiet_NaN();
		Check(controller.Update(invalid, { 100.0, 0.0, 0.0 }).Status == AimStatus::Invalid,
			"non-finite interaction aim observation was accepted");

		RecordingInputTarget target;
		PlayerAimInputAdapter adapter;
		offset.Turn = 0.5;
		offset.LookUp = -0.25;
		adapter.Apply(offset, target);
		Check(adapter.IsActive() && Near(target.Composition.GetAxisValue("aTurn"), 2048.0) &&
			Near(target.Composition.GetAxisValue("aLookUp"), -1024.0),
			"interaction aim did not use independent conventional UE1 axes");
		adapter.Apply(aligned, target);
		Check(!adapter.IsActive() && target.ReleaseControlCount == 2 &&
			Near(target.Composition.GetAxisValue("aTurn"), 0.0) &&
			Near(target.Composition.GetAxisValue("aLookUp"), 0.0),
			"aligned interaction aim did not release only its controls");
	}

	void TestStockInteractionArrivalRadius()
	{
		const std::optional<double> radius = ComputeStockInteractionArrivalRadius(
			{ 0.0, 0.0, 28.0 }, 47.5, { 100.0, 0.0, 30.9 }, 112.0);
		Check(radius && *radius > 90.0 && *radius < 92.0,
			"eye-relative stock interaction radius was not conservatively staged");
		Check(!ComputeStockInteractionArrivalRadius(
			{ 0.0, 0.0, 0.0 }, 40.0, { 0.0, 0.0, 200.0 }, 112.0),
			"vertically impossible stock interaction produced an arrival radius");
		Check(!ComputeStockInteractionArrivalRadius(
			{ 0.0, 0.0, 0.0 }, 40.0, { 1.0, 0.0, 0.0 }, 112.0, 1.0),
			"unbounded stock interaction safety fraction was accepted");
	}
}

int main()
{
	TestSteering();
	TestArrivalTimeoutAndStuck();
	TestResolvedActorContract();
	TestBoundedAbort();
	TestInputCompositionAndRelease();
	TestBoundedInteractionAim();
	TestStockInteractionArrivalRadius();
	if (Failures == 0)
		std::cout << "All player movement controller tests passed.\n";
	return Failures == 0 ? 0 : 1;
}
