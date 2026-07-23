#include "Platform/OpenXR/OpenXRUIRuntime.h"
#include "XR/XRStartupIntroRoute.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace
{
	void Check(bool condition, const char* message)
	{
		if (!condition)
		{
			std::cerr << message << '\n';
			std::exit(1);
		}
	}

	class Host final : public XRUISurfaceEngineHost
	{
	public:
		bool BeginXRUICanvasCapture(const XRUICanvasReplayItem&) override { return true; }
		void ReplayXRUICanvas(const XRUICanvasReplayItem&) override {}
		void EndXRUICanvasCapture(const XRUICanvasReplayItem&) override {}
		void MoveXRUICursor(const XRUIPointerSource& source, XRUISurfaceKind,
			const Pointf&) override { moved.push_back(source); }
		void PressXRUIPrimary(const XRUIPointerSource& source, XRUISurfaceKind,
			const Pointf&) override { pressed.push_back(source); }
		void ReleaseXRUIPrimary(const XRUIPointerSource& source, XRUISurfaceKind,
			const Pointf&, bool canceled) override
		{
			released.push_back(source);
			lastReleaseCanceled = canceled;
		}
		void EndXRUIPointerSession() override { pointerSessionsEnded++; }

		Array<XRUIPointerSource> moved;
		Array<XRUIPointerSource> pressed;
		Array<XRUIPointerSource> released;
		int pointerSessionsEnded = 0;
		bool lastReleaseCanceled = false;
	};

	class Sink final : public OpenXRUICompositionSink
	{
	public:
		bool AllocateSurfaceTargets(
			const std::array<XRUICanvasCaptureDescriptor, 4>& source) override
		{
			descriptors = source;
			allocations++;
			return allowAllocation;
		}
		bool BeginSurfaceFrame(const XRUICanvasReplayFrame& source,
			const std::array<XRUIPointerFeedback, XRHandCount>& sourceFeedback,
			const OpenXRUICompositionSpace& sourceSpace) override
		{
			frame = source;
			feedback = sourceFeedback;
			space = sourceSpace;
			compositions++;
			return allowComposition;
		}
		bool EndSurfaceFrame(bool rendered) override
		{
			finishes++;
			lastRendered = rendered;
			return allowFinish;
		}
		void ReleaseSurfaceTargets() override { releases++; }

		bool allowAllocation = true;
		bool allowComposition = true;
		bool allowFinish = true;
		int allocations = 0;
		int compositions = 0;
		int finishes = 0;
		int releases = 0;
		bool lastRendered = false;
		std::array<XRUICanvasCaptureDescriptor, 4> descriptors;
		XRUICanvasReplayFrame frame;
		std::array<XRUIPointerFeedback, XRHandCount> feedback;
		OpenXRUICompositionSpace space;
	};

	class HapticSink final : public IXRHapticSink
	{
	public:
		bool SubmitHaptic(const XRHapticRequest& request) override
		{
			requests.push_back(request);
			return true;
		}

		std::vector<XRHapticRequest> requests;
	};

	ViewFamily CenteredViews()
	{
		ViewDescription left;
		left.Location = vec3(0.0f, -1.0f, 0.0f);
		left.Rotation = Coords::Identity();
		ViewDescription right = left;
		right.Location.y = 1.0f;
		ViewFamily family;
		family.Views = { left, right };
		return family;
	}

	struct Input
	{
		XRSessionState session{ XRSessionLifecycle::Running, XRSessionFocus::Focused };
		XRControllerSnapshot controllers;
		std::array<XRUISurfaceRay, XRHandCount> rays = {
			XRUISurfaceRay{ vec3(0.0f), vec3(1.0f, 0.0f, 0.0f) },
			XRUISurfaceRay{ vec3(0.0f), vec3(1.0f, 0.0f, 0.0f) }
		};
		std::array<bool, XRHandCount> valid = { true, true };

		Input()
		{
			controllers.ForHand(XRHand::Left).Connected = true;
			controllers.ForHand(XRHand::Right).Connected = true;
		}
	};

	void Update(OpenXRUIRuntime& runtime, XRUISurfaceEngineBinding& binding,
		const Input& input)
	{
		runtime.Update(binding, CenteredViews(), input.session, input.controllers,
			input.rays, input.valid, 40.0f);
	}

	void TestAllocationAndTopmostMenu()
	{
		Host host;
		Sink sink;
		XRUISurfaceEngineBinding binding(host);
		OpenXRUIRuntime runtime;
		Check(runtime.Start(binding, sink, 40.0f), "native UI target allocation failed");
		Check(sink.descriptors[0].Target == OpenXRUIRuntime::SurfaceTargets.Hud &&
			sink.descriptors[1].Target == OpenXRUIRuntime::SurfaceTargets.Cinematic &&
			sink.descriptors[2].Target == OpenXRUIRuntime::SurfaceTargets.Loading &&
			sink.descriptors[3].Target == OpenXRUIRuntime::SurfaceTargets.Menu,
			"native UI target roles diverged from the shared descriptors");
		Update(runtime, binding, Input{});
		binding.SetHudActive(true);
		binding.SetCinematicActive(true);
		binding.SetSurfaceActive(XRUISurfaceKind::Loading, true);
		binding.SetMenuActive(true);
		OpenXRUICompositionSpace space{ true, {}, 0.0f, 40.0f };
		Check(runtime.BeginComposition(binding, sink, space), "native UI composition sink rejected a frame");
		Check(sink.frame.Items.size() == 4 &&
			sink.frame.Items.back().Surface.Descriptor.Kind == XRUISurfaceKind::Menu,
			"menu was not the topmost native UI surface");
		const vec3 submittedRight = sink.frame.Items.back().Surface.Pose.Right;
		Check(std::abs(submittedRight.x) < 0.001f &&
			std::abs(submittedRight.y + 1.0f) < 0.001f &&
			std::abs(submittedRight.z) < 0.001f,
			"native OpenXR composition received WebXR's image-orientation reflection");
		Check(runtime.FinishComposition(sink, true),
			"native UI composition sink rejected frame cleanup");
		Check(sink.finishes == 1 && sink.lastRendered,
			"native UI composition did not finish after replay submission");
		runtime.Stop(binding, sink);
	}

	void TestBothControllersAndHeldTriggerHandoff()
	{
		Host host;
		Sink sink;
		XRUISurfaceEngineBinding binding(host);
		OpenXRUIRuntime runtime;
		XRHapticFeedbackPolicy hapticPolicy;
		HapticSink hapticSink;
		Check(runtime.Start(binding, sink, 40.0f), "native UI runtime did not start");
		binding.SetHudActive(true);
		Input input;
		input.rays[XRHandIndex(XRHand::Left)].Origin.y = -5.0f;
		input.rays[XRHandIndex(XRHand::Right)].Origin.y = 7.0f;
		hapticPolicy.UpdateInput(input.session, input.controllers,
			XRHapticInputContext::UserInterface, &hapticSink);
		Update(runtime, binding, input);
		input.controllers.ForHand(XRHand::Right).Select.Pressed = true;
		hapticPolicy.UpdateInput(input.session, input.controllers,
			XRHapticInputContext::UserInterface, &hapticSink);
		Update(runtime, binding, input);
		ResolveXRUIHapticFeedback(hapticPolicy, runtime.Feedback(), &hapticSink);
		Check(runtime.Feedback()[0].Active && runtime.Feedback()[1].Active,
			"native UI did not retain both tracked controllers");
		Check(!runtime.Feedback()[0].Contact.Hit && !runtime.Feedback()[1].Contact.Hit,
			"non-interactive startup HUD captured a controller");
		Check(hapticSink.requests.empty(),
			"a fresh trigger on a UI miss produced haptic feedback");

		binding.SetHudActive(false);
		binding.SetMenuActive(true);
		hapticPolicy.UpdateInput(input.session, input.controllers,
			XRHapticInputContext::UserInterface, &hapticSink);
		Update(runtime, binding, input);
		ResolveXRUIHapticFeedback(hapticPolicy, runtime.Feedback(), &hapticSink);
		binding.Replay(XRUICanvasReplayContext::Game);
		Check(runtime.Feedback()[0].Contact.Hit && runtime.Feedback()[1].Contact.Hit,
			"both native controller rays did not hit the menu");
		Check(runtime.Feedback()[0].Contact.Pixel.x !=
			runtime.Feedback()[1].Contact.Pixel.x &&
			runtime.Feedback()[0].HitPoint.y != runtime.Feedback()[1].HitPoint.y,
			"asymmetric native rays collapsed to one UI contact");
		const XRUICanvasReplayFrame orderedFrame = binding.BuildReplayFrame();
		Check(orderedFrame.Items.size() == 1 &&
			XRUIControllerVisualCompositionOrder <
				orderedFrame.Items.front().Surface.CompositionOrder &&
			XRUIHitMarkerCompositionOrder >
				orderedFrame.Items.back().Surface.CompositionOrder,
			"native controller/UI/exact-marker composition order changed");
		const XRUIVisualFrame& visuals = runtime.VisualFrame();
		Check(visuals.Hands.size() == 2 &&
			visuals.Hands[0].Hand == XRHand::Left &&
			visuals.Hands[1].Hand == XRHand::Right,
			"native UI did not build provider-neutral geometry for both tracked hands");
		for (size_t hand = 0; hand < XRHandCount; hand++)
		{
			Check(!visuals.Hands[hand].Controller.empty(),
				"native UI omitted tracked controller geometry");
			const bool dominant = visuals.Hands[hand].Hand == XRHand::Right;
			Check(dominant ? (!visuals.Hands[hand].Laser.empty() &&
				!visuals.Hands[hand].HitMarker.empty()) :
				(visuals.Hands[hand].Laser.empty() &&
				visuals.Hands[hand].HitMarker.empty()),
				"native UI did not limit the exact pointer to the dominant hand");
			for (const XRUIVisualVertex& vertex : visuals.Hands[hand].Laser)
				Check(vertex.Position.x <= runtime.Feedback()[hand].HitPoint.x + 0.001f,
					"native beam extended beyond the authoritative UI contact");
			for (size_t vertex = 0; vertex < visuals.Hands[hand].HitMarker.size(); vertex += 3)
				Check(length(visuals.Hands[hand].HitMarker[vertex].Position -
					runtime.Feedback()[hand].HitPoint) < 0.001f,
					"native hit marker was not centered on the authoritative UI contact");
		}
		Check(host.pressed.empty(), "held startup trigger clicked during menu handoff");

		input.controllers.ForHand(XRHand::Right).Select.Pressed = false;
		hapticPolicy.UpdateInput(input.session, input.controllers,
			XRHapticInputContext::UserInterface, &hapticSink);
		Update(runtime, binding, input);
		ResolveXRUIHapticFeedback(hapticPolicy, runtime.Feedback(), &hapticSink);
		binding.Replay(XRUICanvasReplayContext::Game);
		Check(host.pressed.empty() && host.released.empty(),
			"held-trigger handoff synthesized a release without a menu press");
		input.controllers.ForHand(XRHand::Right).Select.Pressed = true;
		hapticPolicy.UpdateInput(input.session, input.controllers,
			XRHapticInputContext::UserInterface, &hapticSink);
		Update(runtime, binding, input);
		ResolveXRUIHapticFeedback(hapticPolicy, runtime.Feedback(), &hapticSink);
		binding.Replay(XRUICanvasReplayContext::Game);
		Check(host.pressed.size() == 1 && host.pressed[0] == XRUIPointerSource::Tracked(2),
			"fresh right trigger edge did not click through exact shared contact");
		Check(hapticSink.requests.size() == 1 &&
			hapticSink.requests[0].Hand == XRHand::Right &&
			hapticSink.requests[0].DurationSeconds ==
				MakeXRHapticOutcomeRequest(XRHapticOutcome::UserInterfaceClick,
					XRHand::Right).DurationSeconds,
			"exact shared menu contact did not produce one UI haptic pulse");

		input.controllers.ForHand(XRHand::Right).Select.Pressed = false;
		Update(runtime, binding, input);
		binding.Replay(XRUICanvasReplayContext::Game);
		binding.UpdateMousePointer(Pointf(512.0f, 384.0f), false);
		Check(!host.moved.empty() && host.moved.back() == XRUIPointerSource::Mouse(),
			"native tracked input disabled the desktop mouse fallback");
		runtime.Stop(binding, sink);
	}

	void TestLoadingScopeCleanupAndMenuOrdering()
	{
		Host host;
		Sink sink;
		XRUISurfaceEngineBinding binding(host);
		OpenXRUIRuntime runtime;
		Check(runtime.Start(binding, sink, 40.0f),
			"native UI runtime did not start for loading scope");
		Update(runtime, binding, Input{});
		binding.SetMenuActive(true);
		{
			XRUILoadingSurfaceScope loading(&binding);
			const XRUICanvasReplayFrame active = binding.BuildReplayFrame();
			Check(active.Items.size() == 2 &&
				active.Items[0].Surface.Descriptor.Kind == XRUISurfaceKind::Loading &&
				active.Items[1].Surface.Descriptor.Kind == XRUISurfaceKind::Menu,
				"loading scope did not preserve the topmost menu");
			const vec3 nativeRight = active.Items.back().Surface.Pose.Right;
			Check(std::abs(nativeRight.x) < 0.001f &&
				std::abs(nativeRight.y + 1.0f) < 0.001f &&
				std::abs(nativeRight.z) < 0.001f,
				"WebXR orientation handling changed native OpenXR's canonical reflected UI basis");
		}
		Check(binding.BuildReplayFrame().Items.size() == 1,
			"completed loading scope did not hide loading");

		try
		{
			XRUILoadingSurfaceScope loading(&binding);
			throw 1;
		}
		catch (int)
		{
		}

		const XRUICanvasReplayFrame cleaned = binding.BuildReplayFrame();
		Check(cleaned.Items.size() == 1 &&
			cleaned.Items[0].Surface.Descriptor.Kind == XRUISurfaceKind::Menu,
			"failed loading scope did not hide loading and preserve the menu");
		XRUILoadingSurfaceScope headless(nullptr);
		runtime.Stop(binding, sink);
	}

	void TestExitAndReentryCleanup()
	{
		Host host;
		Sink sink;
		XRUISurfaceEngineBinding binding(host);
		OpenXRUIRuntime runtime;
		Input input;
		Check(runtime.Start(binding, sink, 40.0f), "first native UI entry failed");
		binding.SetMenuActive(true);
		Update(runtime, binding, input);
		input.controllers.ForHand(XRHand::Left).Select.Pressed = true;
		Update(runtime, binding, input);
		binding.Replay(XRUICanvasReplayContext::Game);
		Check(host.pressed.size() == 1, "first native UI entry did not press");
		runtime.Stop(binding, sink);
		Check(sink.releases == 1 && host.released.size() == 1 && host.lastReleaseCanceled,
			"native UI exit did not cancel capture and release targets");
		Check(binding.BuildReplayFrame().Items.empty(), "native UI exit left surfaces visible");

		input.controllers.ForHand(XRHand::Left).Select.Pressed = false;
		Check(runtime.Start(binding, sink, 40.0f), "native UI re-entry failed");
		Update(runtime, binding, input);
		input.controllers.ForHand(XRHand::Left).Select.Pressed = true;
		Update(runtime, binding, input);
		binding.Replay(XRUICanvasReplayContext::Game);
		Check(host.pressed.size() == 2, "native UI re-entry retained stale pointer state");
		runtime.Stop(binding, sink);

		Sink unavailable;
		unavailable.allowAllocation = false;
		Check(!runtime.Start(binding, unavailable, 40.0f) && !runtime.IsStarted(),
			"runtime claimed UI support when the backend could not allocate targets");
		Check(unavailable.releases == 0,
			"failed allocation released targets it never owned");

		Sink interrupted;
		Check(runtime.Start(binding, interrupted, 40.0f),
			"interrupted native UI runtime did not start");
		Check(runtime.BeginComposition(binding, interrupted,
			{ true, {}, 0.0f, 40.0f }), "interrupted frame did not begin");
		runtime.Stop(binding, interrupted);
		Check(interrupted.finishes == 1 && !interrupted.lastRendered &&
			interrupted.releases == 1,
			"stop did not roll back an active native UI frame before release");
	}

	void TestStartupFireHandoff()
	{
		XRStartupIntroTriggerRoute route;
		XRStartupIntroFireEvent press = route.Update(
			InputSourceId::XRRight, true, true, false);
		Check(press && press.Pressed &&
			press.Control == XRStartupIntroFireControl::Primary,
			"native startup trigger did not mirror primary fire");
		Check(!route.Update(InputSourceId::XRRight, true, false, true),
			"held startup trigger emitted a second edge at menu handoff");
		XRStartupIntroFireEvent release = route.Update(
			InputSourceId::XRRight, false, false, true);
		Check(release && !release.Pressed &&
			release.Control == XRStartupIntroFireControl::Primary,
			"native startup trigger was not balanced after menu handoff");
		Check(!route.ReleaseSource(InputSourceId::XRRight),
			"native startup cleanup emitted a duplicate release");
	}
}

int main()
{
	TestAllocationAndTopmostMenu();
	TestBothControllersAndHeldTriggerHandoff();
	TestLoadingScopeCleanupAndMenuOrdering();
	TestExitAndReentryCleanup();
	TestStartupFireHandoff();
	std::cout << "OpenXR UI runtime tests passed\n";
	return 0;
}
