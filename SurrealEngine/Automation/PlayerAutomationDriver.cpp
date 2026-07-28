#include "Precomp.h"
#include "PlayerAutomationDriver.h"
#include "DeusExSightProbe.h"
#include "PlayerAutomationRunConfig.h"
#include "PlayerMovementController.h"
#include "InteractionActionController.h"
#include "ReachSpecRoutePlanner.h"
#include "Engine.h"
#include "GameSupport/DeusEx/AIPerception.h"
#include "Package/PackageManager.h"
#include "Runtime/HeadlessDriver.h"
#include "UObject/UActor.h"
#include "UObject/UClient.h"
#include "UObject/ULevel.h"
#include "Utils/CommandLine.h"
#include "Utils/File.h"
#include "Utils/Logger.h"
#include "VM/ScriptCall.h"

#include <filesystem>
#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <sstream>
#include <set>
#include <utility>

namespace
{
	using namespace Automation;
	static constexpr size_t MaximumRouteSegments = 64;
	static constexpr uint64_t MaximumIntermediateInteractionTicks = 120;
	static constexpr uint64_t MaximumScriptedObstacleWaitTicks = 600;
	static constexpr double MinimumIntermediateReceiverMovement = 0.001;
	static constexpr double MaximumHorizontalWaypointRecoveryRadius = 32.0;
	static constexpr double MaximumVerticalWaypointRecoveryDistance = 128.0;

	class PlayerAutomationDriver final : public HeadlessDriver
	{
		struct IntermediateReceiverBaseline
		{
			UActor* Actor = nullptr;
			std::string Identity;
			WorldPoint Location;
		};

	public:
		PlayerAutomationDriver(Engine& engine, PlayerAutomationRunConfig config)
			: EngineRef(engine), Config(std::move(config)), ActiveCommand(Config.GetCommand())
		{
		}

		HeadlessDriverConfig GetConfig() const override
		{
			HeadlessDriverConfig config;
			config.Runtime.Seed = Config.GetSeed();
			config.Runtime.FixedDelta = Config.GetFixedDelta();
			config.MaxTicks = Config.GetMaxTicks();
			return config;
		}

		void Start() override
		{
			try
			{
				OpenTelemetry();
				WriteEvent(TelemetryEventType::CommandIssued, 0, 0, {}, {}, {}, {},
					"bounded automation command issued");
				if (!EngineRef.LaunchInfo.IsDeusEx())
				{
					Finalize(CommandState::Rejected, 0, 0,
						"player automation driver currently requires Deus Ex");
					return;
				}

				SetupMapAndPlayer();
				if (Complete)
					return;
				if (Config.IsSightProbe())
				{
					RunSightProbe();
					return;
				}
				const ObservationSnapshot initialObservation =
					CaptureObservation(ObservationRevision, 0, false);
				WriteObservation(initialObservation);

				ValidationResult validation;
				if (ActiveCommand.Kind == CommandKind::Wait)
					validation = Action.Start(ActiveCommand, CaptureMinimalObservation(ObservationRevision, 0));
				else if (ActiveCommand.Kind == CommandKind::WalkToActor)
				{
					const TargetResolutionResult resolved = ResolveTargetForCommand(
						ActiveCommand, initialObservation.Targets, ObservationRevision);
					if (!resolved)
					{
						Finalize(CommandState::Rejected, 0, ObservationRevision,
							"exact walk target was rejected: " + resolved.Error);
						return;
					}
					TrackedTarget = FindActor(*resolved.Target);
					if (!TrackedTarget)
					{
						Finalize(CommandState::Rejected, 0, ObservationRevision,
							"resolved walk target has no unique live engine actor");
						return;
					}
					TrackedTargetSnapshot = *resolved.Target;
					validation = Controller.Start(ActiveCommand, TrackedTargetSnapshot,
						0, ObservationRevision);
					if (validation)
					{
						WriteEvent(TelemetryEventType::TargetResolved, 0, ObservationRevision,
							{}, resolved.Target->Identity, PlayerPosition(), {},
							"exact reachable actor snapshot bound; destination fixed to observation revision 1; " +
							TrackedTargetDiagnostics());
					}
				}
				else if (ActiveCommand.Kind == CommandKind::AcquireItem ||
					ActiveCommand.Kind == CommandKind::Interact)
				{
					const TargetRequirement requirements = ActiveCommand.Kind == CommandKind::AcquireItem ?
						TargetRequirement::Acquirable : TargetRequirement::None;
					const TargetResolutionResult resolved = ResolveTarget(*ActiveCommand.Target,
						initialObservation.Targets, ObservationRevision, requirements);
					if (!resolved)
					{
						Finalize(CommandState::Rejected, 0, ObservationRevision,
							"exact actor target was rejected: " + resolved.Error);
						return;
					}
					TrackedTarget = FindActor(*resolved.Target);
					if (!TrackedTarget)
					{
						Finalize(CommandState::Rejected, 0, ObservationRevision,
							"resolved actor target has no unique live engine actor");
						return;
					}
					TrackedTargetSnapshot = *resolved.Target;
					validation = Action.Start(ActiveCommand, initialObservation);
					if (validation)
					{
						RoutePlanningPending = true;
						SetRouteDetail("exact snapshot actor bound; UE1 navigation warm-up pending; " +
							TrackedTargetDiagnostics());
					}
					if (validation)
						WriteEvent(TelemetryEventType::TargetResolved, 0, ObservationRevision,
							{}, resolved.Target->Identity, PlayerPosition(), {},
							RouteDetail);
				}
				else
					validation = Controller.Start(ActiveCommand, {}, 0, ObservationRevision);
				if (!validation)
				{
					Finalize(CommandState::Rejected, 0, ObservationRevision,
						"automation controller rejected command: " + validation.Error);
					return;
				}
				WriteEvent(TelemetryEventType::CommandAccepted, 0, ObservationRevision,
					CommandState::Accepted,
					ActiveCommand.Target ? ActiveCommand.Target->Identity : std::string(),
					PlayerPosition(), {},
					ActiveCommand.Kind == CommandKind::Wait ?
					"bounded wait accepted" :
					ActiveCommand.Kind == CommandKind::WalkToActor ?
					"exact reachable actor snapshot movement accepted" :
					(ActiveCommand.Kind == CommandKind::AcquireItem ||
						ActiveCommand.Kind == CommandKind::Interact) ?
					"exact actor action accepted" : "viewport player input ownership acquired");
				if (Config.GetCaptureRequest() &&
					Config.GetCaptureRequest()->Phase == AutomationCapturePhase::PreAction)
					CaptureActionBarrier(initialObservation, 0);
			}
			catch (const std::exception& e)
			{
				LogMessage("Player automation start failed: " + std::string(e.what()));
				Finalize(CommandState::Failed, 0, ObservationRevision, e.what());
			}
		}

		bool IsComplete() const override
		{
			return Complete;
		}

		void Tick(const DeterministicFrameTime& frameTime) override
		{
			try
			{
				if (Config.GetCaptureRequest() &&
					Config.GetCaptureRequest()->Phase == AutomationCapturePhase::PreAction &&
					!CaptureCompleted)
				{
					Finalize(CommandState::Failed, 0, ObservationRevision,
						"pre-action capture barrier did not publish before the first tick");
					return;
				}
				if (!ViewportPawn || !EngineRef.viewport || EngineRef.viewport->Actor() != ViewportPawn ||
					ViewportPawn->bDeleteMe())
				{
					Finalize(CommandState::Failed, frameTime.Tick, ObservationRevision,
						"viewport player identity changed during bounded command");
					return;
				}

				ObservationRevision++;
				if (HandleScheduledAbort(frameTime))
					return;
				if (Action.IsActive())
				{
					Ticks = frameTime.Tick;
					const bool actorAction = ActiveCommand.Kind == CommandKind::AcquireItem ||
						ActiveCommand.Kind == CommandKind::Interact;
					if (actorAction && !RefreshStockFrobTarget())
					{
						Finalize(CommandState::Failed, frameTime.Tick, ObservationRevision,
							"Deus Ex pawn does not expose stock HighlightCenterObject selection");
						return;
					}
					if (actorAction && WaypointAction.IsActive())
					{
						HandleIntermediateInteraction(frameTime);
						return;
					}
					if (!actorAction)
						AdvanceLevel(frameTime);
					const ObservationSnapshot observation = actorAction ?
						CaptureObservation(ObservationRevision, frameTime.Tick, true) :
						CaptureMinimalObservation(ObservationRevision, frameTime.Tick);
					InteractionActionStep actionStep = Action.Update(observation);
					if (actorAction && actionStep.State != CommandState::Running)
					{
						if (actionStep.State == CommandState::Succeeded &&
							StartFollowupAcquisition(frameTime, actionStep.Reason))
							return;
						Finalize(actionStep.State, frameTime.Tick, ObservationRevision,
							actionStep.Reason);
						return;
					}
					if (actorAction && actionStep.RequestInteraction)
					{
						Input.Release(EngineRef);
						AimInput.Release(EngineRef);
						EngineRef.ApplyInputCompositionToViewport(frameTime.RealElapsed);
						if (Config.GetCaptureRequest() &&
							(Config.GetCaptureRequest()->Phase ==
								AutomationCapturePhase::PreStockInteraction ||
							 Config.GetCaptureRequest()->Phase ==
								AutomationCapturePhase::PrePickup))
							CaptureActionBarrier(observation, frameTime.Tick);
						WriteEvent(TelemetryEventType::InteractionAttempt, frameTime.Tick,
							ObservationRevision, CommandState::Running,
							ActiveCommand.Target->Identity, PlayerPosition(), {},
							"stock ParseRightClick input pressed for the exact FrobTarget");
						EngineRef.InputEvent(IK_RightMouse, EInputType::IST_Press, 0.0f,
							InputSourceId::Synthetic);
						EngineRef.ApplyInputCompositionToViewport(frameTime.RealElapsed);
						AdvanceLevel(frameTime);
						EngineRef.InputEvent(IK_RightMouse, EInputType::IST_Release, 0.0f,
							InputSourceId::Synthetic);
						EngineRef.ApplyInputCompositionToViewport(frameTime.RealElapsed);
						return;
					}
					if (actorAction && ScriptedObstacleWaitDeadline != 0)
					{
						const bool speechPending = HasPendingDataLinkSpeech();
						if (speechPending && frameTime.Tick < ScriptedObstacleWaitDeadline)
						{
							WriteEvent(TelemetryEventType::CommandProgress, frameTime.Tick,
								ObservationRevision, CommandState::Running,
								ActiveCommand.Target->Identity, PlayerPosition(), {},
								"bounded stock datalink wait in progress before route replan");
							AdvanceLevel(frameTime);
							return;
						}
						ScriptedObstacleWaitDeadline = 0;
						PendingVacatedPassage = FindVacatedPassage(ScriptedMoverBaselines);
						if (PendingVacatedPassage)
							SetRouteDetail("stock scripted mover transition exposed a bounded vacated passage");
						ScriptedMoverBaselines.clear();
						RoutePlanningPending = true;
						WriteEvent(TelemetryEventType::CommandProgress, frameTime.Tick,
							ObservationRevision, CommandState::Running,
							ActiveCommand.Target->Identity, PlayerPosition(), {},
							speechPending ?
							"bounded stock datalink wait expired; replanning route" :
							"stock datalink wait completed; replanning route");
						AdvanceLevel(frameTime);
						return;
					}
					if (actorAction && RoutePlanningPending)
					{
						AimInput.Release(EngineRef);
						if (frameTime.Tick == 1)
						{
							WriteEvent(TelemetryEventType::CommandProgress, frameTime.Tick,
								ObservationRevision, CommandState::Running,
								ActiveCommand.Target->Identity, PlayerPosition(), {},
								"one deterministic world tick reserved for UE1 navigation warm-up");
							AdvanceLevel(frameTime);
							return;
						}
						const ValidationResult route = BeginMovementSegment(frameTime.Tick);
						RoutePlanningPending = false;
						if (!route)
						{
							Finalize(CommandState::Failed, frameTime.Tick, ObservationRevision,
								"route planning failed after warm-up: " + route.Error);
							return;
						}
						WriteEvent(TelemetryEventType::TargetResolved, frameTime.Tick,
							ObservationRevision, CommandState::Running,
							ActiveCommand.Target->Identity, PlayerPosition(), {}, RouteDetail);
					}
					if (actorAction && Controller.IsActive())
					{
						AimInput.Release(EngineRef);
						MovementObservation movementObservation;
						movementObservation.Tick = frameTime.Tick;
						movementObservation.Position = PlayerPosition();
						movementObservation.YawRadians = ViewportPawn->Rotation().YawRadians();
						const MovementStep movementStep = Controller.Update(movementObservation);
						if (movementStep.Status == MovementStatus::Running)
						{
							Input.Apply(movementStep, EngineRef);
							EngineRef.ApplyInputCompositionToViewport(frameTime.RealElapsed);
							WriteEvent(TelemetryEventType::CommandProgress, frameTime.Tick,
								ObservationRevision, CommandState::Running,
								ActiveCommand.Target->Identity, movementObservation.Position,
								movementStep.HorizontalDistance, "approaching exact actor through ordinary player axes");
							AdvanceLevel(frameTime);
							return;
						}

						Input.Release(EngineRef);
						EngineRef.ApplyInputCompositionToViewport(frameTime.RealElapsed);
						if (movementStep.Status == MovementStatus::Arrived)
						{
							if (!MovingToFinalTarget)
							{
								if (MovementMoverBaseline)
								{
									const bool arrivedAtBaselineMover =
										MovementWaypoint == MovementMoverBaseline->Actor;
									const std::vector<IntermediateReceiverBaseline> baseline = {
										*MovementMoverBaseline };
									PendingVacatedPassage = FindVacatedPassage(baseline);
									if (PendingVacatedPassage)
									{
										MovementMoverBaseline.reset();
										MovementWaypoint = nullptr;
										RoutePlanningPending = true;
										SetRouteDetail("selected mover waypoint transitioned; tight traversal through its vacated passage is required");
										WriteEvent(TelemetryEventType::CommandProgress,
											frameTime.Tick, ObservationRevision,
											CommandState::Running,
											ActiveCommand.Target->Identity,
											movementObservation.Position,
											movementStep.HorizontalDistance, RouteDetail);
										AdvanceLevel(frameTime);
										return;
									}
									if (!arrivedAtBaselineMover)
										MovementMoverBaseline.reset();
								}
								if (ShouldInteractWithWaypoint())
								{
									const ValidationResult interaction =
										BeginIntermediateInteraction(frameTime.Tick);
									if (!interaction)
									{
										Finalize(CommandState::Failed, frameTime.Tick,
											ObservationRevision,
											"intermediate interaction rejected: " + interaction.Error);
										return;
									}
									WriteEvent(TelemetryEventType::CommandProgress,
										frameTime.Tick, ObservationRevision,
										CommandState::Running, ActorIdentity(InteractionWaypoint),
										movementObservation.Position, movementStep.HorizontalDistance,
										"bounded intermediate stock interaction started");
									AdvanceLevel(frameTime);
									return;
								}
								RoutePlanningPending = true;
								WriteEvent(TelemetryEventType::CommandProgress, frameTime.Tick,
									ObservationRevision, CommandState::Running,
									ActiveCommand.Target->Identity, movementObservation.Position,
									movementStep.HorizontalDistance, RouteDetail);
								AdvanceLevel(frameTime);
								return;
							}
							WriteEvent(TelemetryEventType::CommandProgress, frameTime.Tick,
								ObservationRevision, CommandState::Running,
								ActiveCommand.Target->Identity, movementObservation.Position,
								movementStep.HorizontalDistance,
								"arrival envelope reached; waiting for stock FrobTarget selection");
							AdvanceLevel(frameTime);
							return;
						}
						if (movementStep.Status == MovementStatus::Stuck &&
							MovementMoverBaseline)
						{
							const std::vector<IntermediateReceiverBaseline> baseline = {
								*MovementMoverBaseline };
							PendingVacatedPassage = FindVacatedPassage(baseline);
							if (PendingVacatedPassage)
							{
								MovementMoverBaseline.reset();
								MovementWaypoint = nullptr;
								RoutePlanningPending = true;
								SetRouteDetail("recent mover waypoint transitioned while the following segment was blocked; tight traversal through its vacated passage is required");
								WriteEvent(TelemetryEventType::CommandProgress,
									frameTime.Tick, ObservationRevision,
									CommandState::Running,
									ActiveCommand.Target->Identity,
									movementObservation.Position, {}, RouteDetail);
								AdvanceLevel(frameTime);
								return;
							}
						}
						if (movementStep.Status == MovementStatus::Stuck &&
							!MovingToFinalTarget && !ShouldInteractWithWaypoint() &&
							movementStep.HorizontalDistance <=
								MaximumHorizontalWaypointRecoveryRadius &&
							movementStep.VerticalDistance <=
								MaximumVerticalWaypointRecoveryDistance)
						{
							MovementMoverBaseline.reset();
							MovementWaypoint = nullptr;
							RoutePlanningPending = true;
							SetRouteDetail("non-interactive spatial waypoint reached horizontally within a bounded vertical envelope; replanning from the progressed floor position");
							WriteEvent(TelemetryEventType::CommandProgress,
								frameTime.Tick, ObservationRevision,
								CommandState::Running,
								ActiveCommand.Target->Identity,
								movementObservation.Position,
								movementStep.HorizontalDistance, RouteDetail);
							AdvanceLevel(frameTime);
							return;
						}
						if (movementStep.Status == MovementStatus::Stuck &&
							BeginScriptedObstacleWait(frameTime.Tick))
						{
							WriteEvent(TelemetryEventType::CommandProgress, frameTime.Tick,
								ObservationRevision, CommandState::Running,
								ActiveCommand.Target->Identity, movementObservation.Position, {},
								"movement blocked while stock datalink speech is active; bounded wait started");
							AdvanceLevel(frameTime);
							return;
						}
						Finalize(movementStep.Status == MovementStatus::TimedOut ?
							CommandState::TimedOut : CommandState::Failed,
							frameTime.Tick, ObservationRevision, movementStep.Reason);
						return;
					}
					if (actionStep.State == CommandState::Running)
					{
						std::string progressDetail = actionStep.Reason;
						if (actorAction)
						{
							const WorldPoint liveTargetLocation = {
								TrackedTarget->Location().x, TrackedTarget->Location().y,
								TrackedTarget->Location().z };
							const AimStep aimStep = InteractionAim.Update(
								CaptureAimObservation(), liveTargetLocation);
							if (aimStep.Status == AimStatus::Invalid)
							{
								Finalize(CommandState::Failed, frameTime.Tick,
									ObservationRevision, "interaction aim observation is invalid");
								return;
							}
							AimInput.Apply(aimStep, EngineRef);
							EngineRef.ApplyInputCompositionToViewport(frameTime.RealElapsed);
							if (aimStep.Status == AimStatus::Aligning)
							{
								progressDetail = "aligning exact target through ordinary player look axes; yaw_error=" +
									std::to_string(aimStep.YawErrorRadians) +
									" pitch_error=" + std::to_string(aimStep.PitchErrorRadians);
							}
						}
						WriteEvent(TelemetryEventType::CommandProgress, frameTime.Tick,
							ObservationRevision, CommandState::Running,
							ActiveCommand.Target ? ActiveCommand.Target->Identity : std::string(),
							PlayerPosition(), {},
							progressDetail);
						if (actorAction)
							AdvanceLevel(frameTime);
						return;
					}
					Finalize(actionStep.State, frameTime.Tick, ObservationRevision, actionStep.Reason);
					return;
				}
				if (ActiveCommand.Kind == CommandKind::WalkToActor &&
					(!TrackedTarget || TrackedTarget->bDeleteMe()))
				{
					Finalize(CommandState::Failed, frameTime.Tick, ObservationRevision,
						"exact walk target is no longer a live engine actor");
					return;
				}
				MovementObservation observation;
				observation.Tick = frameTime.Tick;
				observation.Position = PlayerPosition();
				observation.YawRadians = ViewportPawn->Rotation().YawRadians();
				const MovementStep step = Controller.Update(observation);
				Ticks = frameTime.Tick;

				if (step.Status == MovementStatus::Running)
				{
					const bool firstWalkEffect = Config.GetCaptureRequest() &&
						Config.GetCaptureRequest()->Phase ==
							AutomationCapturePhase::PreAction &&
						!CaptureFirstEffectCompleted;
					if (firstWalkEffect &&
						(EngineRef.SyntheticInputRequestCount() != 0 ||
						 EngineRef.SyntheticInteractionPressCount() != 0))
					{
						Finalize(CommandState::Failed, frameTime.Tick,
							ObservationRevision,
							"walk first-effect barrier observed earlier synthetic input");
						return;
					}
					Input.Apply(step, EngineRef);
					if (firstWalkEffect &&
						(EngineRef.SyntheticInputRequestCount() != 3 ||
						 EngineRef.SyntheticInteractionPressCount() != 0))
					{
						Input.Release(EngineRef);
						EngineRef.ApplyInputCompositionToViewport(frameTime.RealElapsed);
						Finalize(CommandState::Failed, frameTime.Tick,
							ObservationRevision,
							"walk first-effect barrier did not apply exactly three ordinary axes");
						return;
					}
					EngineRef.ApplyInputCompositionToViewport(frameTime.RealElapsed);
					WriteEvent(TelemetryEventType::CommandProgress, frameTime.Tick,
						ObservationRevision, CommandState::Running,
						ActiveCommand.Target ? ActiveCommand.Target->Identity : std::string(),
						observation.Position,
						step.HorizontalDistance, "ordinary player axes applied");
					if (firstWalkEffect)
						CaptureFirstEffectCompleted = true;
					AdvanceLevel(frameTime);
					return;
				}

				Input.Release(EngineRef);
				EngineRef.ApplyInputCompositionToViewport(frameTime.RealElapsed);
				switch (step.Status)
				{
				case MovementStatus::Arrived:
					Finalize(CommandState::Succeeded, frameTime.Tick, ObservationRevision, step.Reason);
					break;
				case MovementStatus::TimedOut:
					Finalize(CommandState::TimedOut, frameTime.Tick, ObservationRevision, step.Reason);
					break;
				case MovementStatus::Stuck:
				case MovementStatus::Invalid:
				default:
					Finalize(CommandState::Failed, frameTime.Tick, ObservationRevision, step.Reason);
					break;
				}
			}
			catch (const std::exception& e)
			{
				LogMessage("Player automation tick failed: " + std::string(e.what()));
				Finalize(CommandState::Failed, frameTime.Tick, ObservationRevision, e.what());
			}
		}

		int Finish(const HeadlessRunSummary& summary) override
		{
			if (!Config.IsSightProbe())
			{
				Input.Release(EngineRef);
				AimInput.Release(EngineRef);
				EngineRef.ApplyInputCompositionToViewport(Config.GetFixedDelta());
			}
			if (summary.TickLimitReached && !Complete)
				Finalize(CommandState::TimedOut, summary.FinalFrame.Tick, ObservationRevision,
					"headless runner reached the bounded tick limit");

			try
			{
				std::filesystem::create_directories(Config.GetOutputDirectory());
				if (Config.IsSightProbe() && SightProbeObservation)
					WriteObservation(*SightProbeObservation, "final-observation.json");
				else if (ViewportPawn && !ViewportPawn->bDeleteMe())
				{
					WriteObservation(CaptureObservation(ObservationRevision,
						Result.Tick, false), "final-observation.json");
				}
				const std::filesystem::path summaryPath =
					std::filesystem::path(Config.GetOutputDirectory()) / "summary.json";
				File::write_all_text(summaryPath.string(), CommandResultJson(Result));
				LogMessage("Player automation summary: " + summaryPath.string());
				if (Config.HasFollowupCommand())
				{
					const std::filesystem::path sequenceSummaryPath =
						std::filesystem::path(Config.GetOutputDirectory()) /
						"sequence-summary.json";
					File::write_all_text(sequenceSummaryPath.string(), SequenceSummaryJson());
					LogMessage("Player automation sequence summary: " +
						sequenceSummaryPath.string());
				}
			}
			catch (const std::exception& e)
			{
				LogMessage("Player automation summary failed: " + std::string(e.what()));
				return 3;
			}
			if (Config.HasCaptureRequest() && !CaptureCompleted)
			{
				LogMessage("Player automation capture failed: requested tick was not presented");
				return 3;
			}
			return Result.State == CommandState::Succeeded ? 0 : 2;
		}

	private:
		void RunSightProbe()
		{
			const TargetSelector& selector = *Config.GetSightProbeTarget();
			ObservationSnapshot observation;
			observation.Revision = ObservationRevision;
			observation.Tick = 0;
			observation.PlayerIdentity = ActorIdentity(ViewportPawn);
			observation.PlayerPosition = PlayerPosition();
			SightProbeObservation = observation;
			UActor* target = FindActor(selector);
			if (!target || target == ViewportPawn)
			{
				WriteObservation(observation);
				Finalize(CommandState::Rejected, 0, ObservationRevision,
					"exact sight probe target has no unique live engine actor");
				return;
			}

			TargetSnapshot snapshot;
			snapshot.ObservationRevision = ObservationRevision;
			snapshot.Identity = ActorIdentity(target);
			snapshot.ClassName = UObject::GetUClassFullName(target).ToString();
			snapshot.OwnerIdentity = ActorIdentity(target->Owner());
			snapshot.StateToken = target->GetStateName().ToString();
			snapshot.TagName = target->Tag().IsNone() ?
				std::string() : target->Tag().ToString();
			snapshot.EventName = target->Event().IsNone() ?
				std::string() : target->Event().ToString();
			snapshot.Location = {
				target->Location().x, target->Location().y, target->Location().z };
			observation.Targets.push_back(snapshot);
			SightProbeObservation = observation;
			WriteObservation(observation);

			DeusExSightProbeRecord record;
			record.ConfigIdentity = Config.ConfigIdentity();
			record.ObservationRevision = ObservationRevision;
			record.Observer = {
				ActorIdentity(ViewportPawn),
				UObject::GetUClassFullName(ViewportPawn).ToString(),
				PlayerPosition() };
			record.ObserverEyeLocation = PlayerPosition();
			record.ObserverEyeLocation.Z += ViewportPawn->EyeHeight();
			const Rotator viewRotation = normalize(ViewportPawn->ViewRotation());
			const Rotator addedRotation = ViewportPawn->AIAddViewRotation();
			record.ViewPitchRadians = viewRotation.PitchRadians();
			record.ViewYawRadians = viewRotation.YawRadians();
			record.ViewRollRadians = viewRotation.RollRadians();
			record.AddedViewPitchRadians = addedRotation.PitchRadians();
			record.AddedViewYawRadians = addedRotation.YawRadians();
			record.AddedViewRollRadians = addedRotation.RollRadians();
			record.HorizontalFovDegrees = ViewportPawn->AIHorizontalFov();
			record.AspectRatio = ViewportPawn->AspectRatio();
			record.MinimumAngularSize = ViewportPawn->MinAngularSize();
			record.VisibilityThreshold = ViewportPawn->VisibilityThreshold();
			record.Target = {
				snapshot.Identity, snapshot.ClassName, snapshot.Location };
			record.TargetDetectable = target->bDetectable();
			record.TargetCollisionRadius = target->CollisionRadius();
			record.TargetCollisionHeight = target->CollisionHeight();
			record.ScalarResult = ViewportPawn->AICanSee(
				target, 1.0f, false, false, false, false);
			record.DirectionResult = ViewportPawn->AICanSee(
				target, 1.0f, false, true, false, false);
			UPawn* targetPawn = UObject::TryCast<UPawn>(target);
			const DXAISightTracePlan tracePlan = BuildDXAISightTracePlan(
				targetPawn != nullptr, targetPawn ? targetPawn->EyeHeight() : 0.0f,
				target->CollisionHeight());
			if (!tracePlan.Valid)
				throw std::runtime_error("sight probe target has invalid LOS geometry");
			const vec3 primary = target->Location() +
				vec3(0.0f, 0.0f, tracePlan.PrimaryZOffset);
			const vec3 top = target->Location() +
				vec3(0.0f, 0.0f, tracePlan.TopZOffset);
			const vec3 bottom = target->Location() +
				vec3(0.0f, 0.0f, tracePlan.BottomZOffset);
			record.LineOfSightPrimary = { primary.x, primary.y, primary.z };
			record.LineOfSightTop = { top.x, top.y, top.z };
			record.LineOfSightBottom = { bottom.x, bottom.y, bottom.z };
			DXAISightLineOfSightResult lineOfSightTrace;
			record.LineOfSightResult = ViewportPawn->AICanSee(
				target, 1.0f, false, false, false, true, &lineOfSightTrace);
			DXAISightLineOfSightResult cylinderLineOfSightTrace;
			record.CylinderLineOfSightResult = ViewportPawn->AICanSee(
				target, 1.0f, false, false, true, true,
				&cylinderLineOfSightTrace);
			record.PrimaryVisible = cylinderLineOfSightTrace.PrimaryVisible;
			record.TopTested = cylinderLineOfSightTrace.TopTested;
			record.TopVisible = cylinderLineOfSightTrace.TopVisible;
			record.BottomTested = cylinderLineOfSightTrace.BottomTested;
			record.BottomVisible = cylinderLineOfSightTrace.BottomVisible;
			record.LineOfSightTraceCount = lineOfSightTrace.TraceCount;
			record.CylinderLineOfSightTraceCount =
				cylinderLineOfSightTrace.TraceCount;

			const std::string artifact = DeusExSightProbeJson(record);
			const std::string digest = DeusExSightProbeDigest(record);
			const std::filesystem::path artifactPath =
				std::filesystem::path(Config.GetOutputDirectory()) / "sight-probe.json";
			File::write_all_text(artifactPath.string(), artifact);
			LogMessage("Deus Ex sight probe: " + artifactPath.string() + " (" + digest + ")");

			WriteEvent(TelemetryEventType::TargetResolved, 0, ObservationRevision,
				{}, snapshot.Identity, PlayerPosition(), {},
				"exact live sight target resolved without probe navigation or synthetic input");
			WriteEvent(TelemetryEventType::CommandAccepted, 0, ObservationRevision,
				CommandState::Accepted, snapshot.Identity, PlayerPosition(), {},
				"bounded Deus Ex sight probe accepted; collision bookkeeping may change");
			WriteEvent(TelemetryEventType::CommandProgress, 0, ObservationRevision,
				CommandState::Running, snapshot.Identity, PlayerPosition(), {},
				"sight-probe.json " + digest +
				" scalar=" + std::to_string(record.ScalarResult) +
				" direction=" + std::to_string(record.DirectionResult) +
				" los=" + std::to_string(record.LineOfSightResult) +
				" cylinder_los=" +
					std::to_string(record.CylinderLineOfSightResult));
			Finalize(CommandState::Succeeded, 0, ObservationRevision,
				"bounded scalar, direction, and LOS sight probes recorded without advancing the level");
		}

		bool HandleScheduledAbort(const DeterministicFrameTime& frameTime)
		{
			const std::optional<AutomationCommand>& scheduled = Config.GetAbortCommand();
			if (!scheduled || frameTime.Tick != scheduled->IssuedTick)
				return false;

			WriteEvent(TelemetryEventType::CommandIssued, frameTime.Tick,
				ObservationRevision, {}, {}, PlayerPosition(), {},
				"bounded abort command issued", &*scheduled);

			ValidationResult validation;
			std::string cancellationReason =
				"active movement command cancelled by bounded abort";
			if (Action.IsActive())
			{
				InteractionActionStep cancelledStep;
				validation = Action.Abort(*scheduled, frameTime.Tick,
					ObservationRevision, cancelledStep);
				if (validation)
					cancellationReason = cancelledStep.Reason;
				if (validation && Controller.IsActive())
					validation = Controller.Abort(*scheduled, frameTime.Tick,
						ObservationRevision);
			}
			else if (Controller.IsActive())
			{
				validation = Controller.Abort(*scheduled, frameTime.Tick,
					ObservationRevision);
			}
			else
			{
				validation = { ValidationStatus::InvalidAbort,
					"scheduled abort found no active command" };
			}

			if (!validation)
			{
				WriteEvent(TelemetryEventType::CommandResult, frameTime.Tick,
					ObservationRevision, CommandState::Rejected, {}, PlayerPosition(), {},
					validation.Error, &*scheduled);
				Finalize(CommandState::Failed, frameTime.Tick, ObservationRevision,
					"scheduled abort failed: " + validation.Error);
				return true;
			}

			WriteEvent(TelemetryEventType::CommandAccepted, frameTime.Tick,
				ObservationRevision, CommandState::Accepted, {}, PlayerPosition(), {},
				"abort accepted for the named active command", &*scheduled);
			Input.Release(EngineRef);
			AimInput.Release(EngineRef);
			EngineRef.ApplyInputCompositionToViewport(frameTime.RealElapsed);
			Finalize(CommandState::Cancelled, frameTime.Tick, ObservationRevision,
				cancellationReason);
			WriteEvent(TelemetryEventType::CommandResult, frameTime.Tick,
				ObservationRevision, CommandState::Succeeded, {}, PlayerPosition(), {},
				"named active command cancelled and synthetic input released", &*scheduled);
			return true;
		}

		void OpenTelemetry()
		{
			std::filesystem::create_directories(Config.GetOutputDirectory());
			const std::filesystem::path outputDirectory(Config.GetOutputDirectory());
			const std::filesystem::path manifestPath = outputDirectory / "manifest.json";
			File::write_all_text(manifestPath.string(), Config.ManifestJson());
			const std::filesystem::path eventsPath = outputDirectory / "events.jsonl";
			TelemetryFile = File::create_always(eventsPath.string());
			LogMessage("Player automation manifest: " + manifestPath.string());
			LogMessage("Player automation telemetry: " + eventsPath.string());
		}

		void SetupMapAndPlayer()
		{
			EngineRef.LaunchInfo.noEntryMap = true;
			EngineRef.LaunchInfo.url = Config.GetURL();
			const UnrealURL base = EngineRef.GetDefaultURL(
				EngineRef.packages->GetIniValue("system", "URL", "LocalMap"));
			EngineRef.LoadMap(UnrealURL(base, Config.GetURL()));
			EngineRef.LoginPlayer();
			ViewportPawn = EngineRef.viewport ? EngineRef.viewport->Actor() : nullptr;
			if (!ViewportPawn || ViewportPawn->bDeleteMe())
				Finalize(CommandState::Failed, 0, ObservationRevision,
					"Deus Ex viewport login did not produce a live player pawn");
			if (!Complete)
				InitializeActorIdentities();
		}

		bool StartFollowupAcquisition(const DeterministicFrameTime& frameTime,
			const std::string& completedReason)
		{
			if (!Config.HasFollowupCommand() || FollowupStarted)
				return false;

			Input.Release(EngineRef);
			AimInput.Release(EngineRef);
			EngineRef.ApplyInputCompositionToViewport(frameTime.RealElapsed);
			const ObservationSnapshot observation = CaptureObservation(
				ObservationRevision, frameTime.Tick, false);

			const AutomationCommand completedCommand = ActiveCommand;
			const CommandResult completed = BuildResult(CommandState::Succeeded,
				frameTime.Tick, ObservationRevision, completedReason);
			Results.push_back(completed);
			WriteEvent(TelemetryEventType::CommandResult, frameTime.Tick,
				ObservationRevision, completed.State, completed.TargetIdentity,
				PlayerPosition(), {}, completed.Reason, &completedCommand);

			ActiveCommand = Config.MaterializeFollowupCommand(
				frameTime.Tick, ObservationRevision);
			FollowupStarted = true;
			ResetActiveCommandState();
			WriteObservation(observation, "followup-observation.json");
			WriteMaterializedFollowupCommand();
			WriteEvent(TelemetryEventType::CommandIssued, frameTime.Tick,
				ObservationRevision, {}, ActiveCommand.Target->Identity,
				PlayerPosition(), {},
				"bounded same-class follow-up acquisition issued from a fresh observation");

			const TargetResolutionResult resolved = ResolveTarget(*ActiveCommand.Target,
				observation.Targets, ObservationRevision, TargetRequirement::Acquirable);
			if (!resolved)
			{
				Finalize(CommandState::Rejected, frameTime.Tick, ObservationRevision,
					"exact follow-up acquisition target was rejected: " + resolved.Error);
				return true;
			}
			TrackedTarget = FindActor(*resolved.Target);
			if (!TrackedTarget)
			{
				Finalize(CommandState::Rejected, frameTime.Tick, ObservationRevision,
					"resolved follow-up target has no unique live engine actor");
				return true;
			}
			TrackedTargetSnapshot = *resolved.Target;
			const ValidationResult validation = Action.Start(ActiveCommand, observation);
			if (!validation)
			{
				Finalize(CommandState::Rejected, frameTime.Tick, ObservationRevision,
					"follow-up acquisition controller rejected command: " +
					validation.Error);
				return true;
			}

			RoutePlanningPending = true;
			SetRouteDetail("exact follow-up snapshot actor bound; UE1 navigation replan pending; " +
				TrackedTargetDiagnostics());
			WriteEvent(TelemetryEventType::TargetResolved, frameTime.Tick,
				ObservationRevision, CommandState::Running,
				resolved.Target->Identity, PlayerPosition(), {}, RouteDetail);
			WriteEvent(TelemetryEventType::CommandAccepted, frameTime.Tick,
				ObservationRevision, CommandState::Accepted,
				resolved.Target->Identity, PlayerPosition(), {},
				"exact same-class follow-up acquisition accepted");
			return true;
		}

		void ResetActiveCommandState()
		{
			Controller.Reset();
			Action.Reset();
			WaypointAction.Reset();
			TrackedTarget = nullptr;
			MovementWaypoint = nullptr;
			InteractionWaypoint = nullptr;
			TrackedTargetSnapshot = {};
			IntermediateReceiverBaselines.clear();
			ScriptedMoverBaselines.clear();
			MovementMoverBaseline.reset();
			PendingVacatedPassage.reset();
			ScriptedObstacleWaitDeadline = 0;
			ScriptedObstacleWaitUsed = false;
			VisitedWaypoints.clear();
			RouteSegmentCount = 0;
			MovingToFinalTarget = false;
			RoutePlanningPending = false;
			RouteDetail.clear();
		}

		void WriteMaterializedFollowupCommand()
		{
			std::ostringstream out;
			out << "{\n"
				<< "  \"schema\": \"surreal-player-automation-materialized-command-v1\",\n"
				<< "  \"config_identity\": \"" << Config.ConfigIdentity() << "\",\n"
				<< "  \"command_digest\": \"" << CommandDigest(ActiveCommand) << "\",\n"
				<< "  \"command\": " << CommandJson(ActiveCommand)
				<< "}\n";
			const std::filesystem::path path =
				std::filesystem::path(Config.GetOutputDirectory()) / "followup-command.json";
			File::write_all_text(path.string(), out.str());
			LogMessage("Player automation follow-up command: " + path.string() +
				" (" + CommandDigest(ActiveCommand) + ")");
		}

		std::string ActorIdentity(UActor* actor) const
		{
			if (!actor)
				return {};
			auto existing = ActorIdentities.find(actor);
			if (existing != ActorIdentities.end())
				return existing->second;
			const std::string base = "actor:" + UObject::GetUClassFullName(actor).ToString() +
				":" + actor->Name.ToString();
			const size_t ordinal = NextActorOrdinal[base]++;
			const std::string identity = base + "#" + std::to_string(ordinal);
			ActorIdentities[actor] = identity;
			return identity;
		}

		void InitializeActorIdentities()
		{
			ActorIdentities.clear();
			NextActorOrdinal.clear();
			if (EngineRef.Level)
			{
				for (UActor* actor : EngineRef.Level->Actors)
				{
					if (actor)
						(void)ActorIdentity(actor);
				}
			}
			(void)ActorIdentity(ViewportPawn);
		}

		UActor* FindActor(const TargetSnapshot& target) const
		{
			UActor* match = nullptr;
			for (const auto& entry : ActorIdentities)
			{
				UActor* actor = entry.first;
				if (!actor || actor->bDeleteMe() || entry.second != target.Identity ||
					UObject::GetUClassFullName(actor).ToString() != target.ClassName)
					continue;
				if (match)
					return nullptr;
				match = actor;
			}
			return match;
		}

		UActor* FindActor(const TargetSelector& selector) const
		{
			UActor* match = nullptr;
			for (const auto& entry : ActorIdentities)
			{
				UActor* actor = entry.first;
				if (!actor || actor->bDeleteMe() || entry.second != selector.Identity ||
					UObject::GetUClassFullName(actor).ToString() != selector.ExpectedClass)
					continue;
				if (match)
					return nullptr;
				match = actor;
			}
			return match;
		}

		ValidationResult BeginMovementSegment(uint64_t currentTick)
		{
			MovementWaypoint = nullptr;
			if (!ViewportPawn || !TrackedTarget || TrackedTarget->bDeleteMe())
				return { ValidationStatus::InvalidTarget,
					"tracked route target is no longer a live actor" };
			if (RouteSegmentCount >= MaximumRouteSegments)
				return { ValidationStatus::InvalidTarget,
					"bounded navigation segment limit reached" };

			ValidationResult validation = Controller.Start(ActiveCommand,
				TrackedTargetSnapshot, currentTick, TrackedTargetSnapshot.ObservationRevision);
			if (!validation)
				return validation;
			if (PendingVacatedPassage)
			{
				const WorldPoint destination = *PendingVacatedPassage;
				PendingVacatedPassage.reset();
				MovingToFinalTarget = false;
				RouteSegmentCount++;
				SetRouteDetail("ordinary input routed through the correlated mover's vacated passage");
				return Controller.Retarget(destination, 32.0, currentTick);
			}

			WorldPoint destination = TrackedTargetSnapshot.Location;
			double arrivalRadius = *ActiveCommand.ArrivalRadius;
			arrivalRadius = std::max(arrivalRadius,
				static_cast<double>(TrackedTarget->CollisionRadius() +
					ViewportPawn->CollisionRadius() + 4.0f));
			if (UDeusExPlayer* player = UObject::TryCast<UDeusExPlayer>(ViewportPawn))
			{
				const std::optional<double> interactionArrivalRadius =
					ComputeStockInteractionArrivalRadius(PlayerPosition(), ViewportPawn->EyeHeight(),
						destination, player->MaxFrobDistance());
				if (interactionArrivalRadius)
					arrivalRadius = std::max(arrivalRadius, *interactionArrivalRadius);
			}
			arrivalRadius = std::min(arrivalRadius, MaximumArrivalRadius);
			const vec3 targetLocation(destination.X, destination.Y, destination.Z);
			if (!ViewportPawn->PointReachable(targetLocation))
			{
				UObject* routeEndpoint = TrackedTarget;
				UInventory* trackedInventory = UObject::TryCast<UInventory>(TrackedTarget);
				if (trackedInventory && trackedInventory->myMarker() &&
					!trackedInventory->myMarker()->bDeleteMe())
				{
					routeEndpoint = trackedInventory->myMarker();
				}
				else if (trackedInventory)
				{
					for (UNavigationPoint* navPoint = ViewportPawn->Level()->NavigationPointList();
						navPoint; navPoint = navPoint->nextNavigationPoint())
					{
						UInventorySpot* inventorySpot = UObject::TryCast<UInventorySpot>(navPoint);
						if (inventorySpot && inventorySpot->markedItem() == TrackedTarget)
						{
							routeEndpoint = inventorySpot;
							break;
						}
					}
				}
				UActor* waypoint = UObject::TryCast<UActor>(
					ViewportPawn->FindPathToward(routeEndpoint, false));
				std::string waypointSource = "UE1 waypoint ";
				std::string routeError;
				if (!waypoint)
				{
					waypoint = FindReachSpecWaypoint(routeError);
					if (waypoint)
						waypointSource = "bounded reach-spec waypoint ";
				}
				if (!waypoint)
				{
					std::string spatialError;
					waypoint = FindReachableSpatialWaypoint(spatialError);
					if (waypoint)
						waypointSource = "bounded reachable actor waypoint ";
					else if (!spatialError.empty())
						routeError += "; " + spatialError;
				}
				if (!waypoint || waypoint->bDeleteMe())
				{
					MovingToFinalTarget = true;
					SetRouteDetail("UE1 navigation unavailable (" + routeError +
						"); bounded local obstacle recovery routed toward the exact target");
					RouteSegmentCount++;
					return Controller.Retarget(destination, arrivalRadius, currentTick);
				}
				const std::string waypointIdentity = ActorIdentity(waypoint);
				if (!VisitedWaypoints.insert(waypointIdentity).second)
					return { ValidationStatus::InvalidTarget,
						"UE1 navigation repeated a visited waypoint" };
				destination = { waypoint->Location().x, waypoint->Location().y,
					waypoint->Location().z };
				arrivalRadius = std::clamp(
					static_cast<double>(waypoint->CollisionRadius() +
						ViewportPawn->CollisionRadius() + 4.0f), 32.0, 128.0);
				MovingToFinalTarget = false;
				MovementWaypoint = waypoint;
				if (UObject::GetUClassFullName(waypoint).ToString() == "DeusEx.DeusExMover")
				{
					MovementMoverBaseline = IntermediateReceiverBaseline{ waypoint,
						waypointIdentity, destination };
				}
				SetRouteDetail(std::string("ordinary input routed toward ") +
					waypointSource + waypointIdentity);
			}
			else
			{
				MovingToFinalTarget = true;
				SetRouteDetail("ordinary input routed toward the directly reachable exact target");
			}
			RouteSegmentCount++;
			return Controller.Retarget(destination, arrivalRadius, currentTick);
		}

		bool ShouldInteractWithWaypoint() const
		{
			return ActiveCommand.Kind == CommandKind::AcquireItem &&
				MovementWaypoint && !MovementWaypoint->bDeleteMe() &&
				!MovementWaypoint->Event().IsNone() &&
				MovementWaypoint->HasProperty("bHighlight") &&
				MovementWaypoint->GetBool("bHighlight");
		}

		bool HasPendingDataLinkSpeech() const
		{
			if (!EngineRef.Level)
				return false;
			for (UActor* actor : EngineRef.Level->Actors)
			{
				if (actor && !actor->bDeleteMe() &&
					UObject::GetUClassFullName(actor).ToString() == "DeusEx.DataLinkPlay" &&
					actor->GetStateName() == NameString("WaitForSpeech"))
					return true;
			}
			return false;
		}

		bool BeginScriptedObstacleWait(uint64_t currentTick)
		{
			if (ActiveCommand.Kind != CommandKind::AcquireItem ||
				ScriptedObstacleWaitUsed || !HasPendingDataLinkSpeech() ||
				currentTick >= ActiveCommand.DeadlineTick)
				return false;
			ScriptedObstacleWaitUsed = true;
			ScriptedObstacleWaitDeadline = std::min(ActiveCommand.DeadlineTick,
				currentTick + MaximumScriptedObstacleWaitTicks);
			ScriptedMoverBaselines.clear();
			for (UActor* actor : EngineRef.Level->Actors)
			{
				if (!actor || actor->bDeleteMe() ||
					UObject::GetUClassFullName(actor).ToString() != "DeusEx.DeusExMover")
					continue;
				if (ScriptedMoverBaselines.size() >= MaximumSpatialWaypointCandidates)
					break;
				ScriptedMoverBaselines.push_back({ actor, ActorIdentity(actor),
					{ actor->Location().x, actor->Location().y, actor->Location().z } });
			}
			std::sort(ScriptedMoverBaselines.begin(), ScriptedMoverBaselines.end(),
				[](const IntermediateReceiverBaseline& left,
					const IntermediateReceiverBaseline& right)
				{
					return left.Identity < right.Identity;
				});
			return ScriptedObstacleWaitDeadline > currentTick;
		}

		ValidationResult BeginIntermediateInteraction(uint64_t currentTick)
		{
			if (!ShouldInteractWithWaypoint())
				return { ValidationStatus::InvalidTarget,
					"waypoint is not eligible for stock interaction" };
			if (currentTick >= ActiveCommand.DeadlineTick)
				return { ValidationStatus::InvalidTickWindow,
					"main command has no remaining intermediate interaction lease" };

			InteractionWaypoint = MovementWaypoint;
			IntermediateReceiverBaselines.clear();
			for (UActor* actor : EngineRef.Level->Actors)
			{
				if (!actor || actor->bDeleteMe() ||
					actor->Tag() != InteractionWaypoint->Event())
					continue;
				if (IntermediateReceiverBaselines.size() >= MaximumSpatialWaypointCandidates)
				{
					InteractionWaypoint = nullptr;
					IntermediateReceiverBaselines.clear();
					return { ValidationStatus::InvalidTarget,
						"intermediate event receiver set exceeds deterministic bounds" };
				}
				IntermediateReceiverBaselines.push_back({ actor, ActorIdentity(actor),
					{ actor->Location().x, actor->Location().y, actor->Location().z } });
			}
			std::sort(IntermediateReceiverBaselines.begin(),
				IntermediateReceiverBaselines.end(),
				[](const IntermediateReceiverBaseline& left,
					const IntermediateReceiverBaseline& right)
				{
					return left.Identity < right.Identity;
				});
			AutomationCommand interaction;
			interaction.Id = "interact-waypoint-" + std::to_string(RouteSegmentCount);
			interaction.Kind = CommandKind::Interact;
			interaction.IssuedTick = currentTick;
			interaction.DeadlineTick = std::min(ActiveCommand.DeadlineTick,
				currentTick + MaximumIntermediateInteractionTicks);
			interaction.Target = TargetSelector{ ObservationRevision,
				ActorIdentity(InteractionWaypoint),
				UObject::GetUClassFullName(InteractionWaypoint).ToString() };
			interaction.ArrivalRadius = std::clamp(
				static_cast<double>(InteractionWaypoint->CollisionRadius() +
					ViewportPawn->CollisionRadius() + 4.0f), 16.0, 128.0);

			const ObservationSnapshot observation =
				CaptureObservation(ObservationRevision, currentTick, true);
			const ValidationResult validation = WaypointAction.Start(interaction, observation);
			if (!validation)
			{
				InteractionWaypoint = nullptr;
				IntermediateReceiverBaselines.clear();
			}
			return validation;
		}

		std::optional<WorldPoint> FindVacatedPassage(
			const std::vector<IntermediateReceiverBaseline>& baselines) const
		{
			const IntermediateReceiverBaseline* best = nullptr;
			double bestDistance = std::numeric_limits<double>::infinity();
			const WorldPoint origin = PlayerPosition();
			for (const IntermediateReceiverBaseline& baseline : baselines)
			{
				if (!baseline.Actor || baseline.Actor->bDeleteMe())
					continue;
				const double movedX = baseline.Actor->Location().x - baseline.Location.X;
				const double movedY = baseline.Actor->Location().y - baseline.Location.Y;
				const double movedZ = baseline.Actor->Location().z - baseline.Location.Z;
				const double movementSquared =
					movedX * movedX + movedY * movedY + movedZ * movedZ;
				if (movementSquared <= MinimumIntermediateReceiverMovement *
					MinimumIntermediateReceiverMovement)
					continue;
				const double deltaX = baseline.Location.X - origin.X;
				const double deltaY = baseline.Location.Y - origin.Y;
				const double deltaZ = baseline.Location.Z - origin.Z;
				const double distance = deltaX * deltaX + deltaY * deltaY + deltaZ * deltaZ;
				if (distance < bestDistance ||
					(distance == bestDistance && best && baseline.Identity < best->Identity))
				{
					best = &baseline;
					bestDistance = distance;
				}
			}
			return best ? std::optional<WorldPoint>(best->Location) : std::nullopt;
		}

		void HandleIntermediateInteraction(const DeterministicFrameTime& frameTime)
		{
			if (!InteractionWaypoint || InteractionWaypoint->bDeleteMe())
			{
				Finalize(CommandState::Failed, frameTime.Tick, ObservationRevision,
					"intermediate interaction waypoint is no longer live");
				return;
			}

			const std::string waypointIdentity = ActorIdentity(InteractionWaypoint);
			const ObservationSnapshot observation =
				CaptureObservation(ObservationRevision, frameTime.Tick, true);
			const InteractionActionStep step = WaypointAction.Update(observation);
			if (step.State != CommandState::Running)
			{
				AimInput.Release(EngineRef);
				EngineRef.ApplyInputCompositionToViewport(frameTime.RealElapsed);
				if (step.State != CommandState::Succeeded)
				{
					Finalize(step.State, frameTime.Tick, ObservationRevision,
						"intermediate interaction failed: " + step.Reason);
					return;
				}
				WriteEvent(TelemetryEventType::CommandProgress, frameTime.Tick,
					ObservationRevision, CommandState::Running, waypointIdentity,
					PlayerPosition(), {}, step.Reason + "; replanning acquisition route");
				PendingVacatedPassage = FindVacatedPassage(IntermediateReceiverBaselines);
				if (PendingVacatedPassage)
					SetRouteDetail("correlated mover transition exposed a bounded vacated passage");
				IntermediateReceiverBaselines.clear();
				InteractionWaypoint = nullptr;
				MovementWaypoint = nullptr;
				RoutePlanningPending = true;
				AdvanceLevel(frameTime);
				return;
			}

			if (step.RequestInteraction)
			{
				Input.Release(EngineRef);
				AimInput.Release(EngineRef);
				EngineRef.ApplyInputCompositionToViewport(frameTime.RealElapsed);
				WriteEvent(TelemetryEventType::InteractionAttempt, frameTime.Tick,
					ObservationRevision, CommandState::Running, waypointIdentity,
					PlayerPosition(), {},
					"stock ParseRightClick input pressed for the intermediate FrobTarget");
				EngineRef.InputEvent(IK_RightMouse, EInputType::IST_Press, 0.0f,
					InputSourceId::Synthetic);
				EngineRef.ApplyInputCompositionToViewport(frameTime.RealElapsed);
				AdvanceLevel(frameTime);
				EngineRef.InputEvent(IK_RightMouse, EInputType::IST_Release, 0.0f,
					InputSourceId::Synthetic);
				EngineRef.ApplyInputCompositionToViewport(frameTime.RealElapsed);
				return;
			}

			const WorldPoint waypointLocation = { InteractionWaypoint->Location().x,
				InteractionWaypoint->Location().y, InteractionWaypoint->Location().z };
			const AimStep aimStep = InteractionAim.Update(
				CaptureAimObservation(), waypointLocation);
			if (aimStep.Status == AimStatus::Invalid)
			{
				Finalize(CommandState::Failed, frameTime.Tick, ObservationRevision,
					"intermediate interaction aim observation is invalid");
				return;
			}
			AimInput.Apply(aimStep, EngineRef);
			EngineRef.ApplyInputCompositionToViewport(frameTime.RealElapsed);
			std::string detail = "waiting for the intermediate waypoint to become the exact FrobTarget";
			if (aimStep.Status == AimStatus::Aligning)
				detail = "aligning intermediate waypoint through ordinary player look axes; yaw_error=" +
					std::to_string(aimStep.YawErrorRadians) + " pitch_error=" +
					std::to_string(aimStep.PitchErrorRadians);
			WriteEvent(TelemetryEventType::CommandProgress, frameTime.Tick,
				ObservationRevision, CommandState::Running, waypointIdentity,
				PlayerPosition(), {}, detail);
			AdvanceLevel(frameTime);
		}

		void SetRouteDetail(std::string detail)
		{
			if (detail.size() > MaximumDetailBytes)
			{
				detail.resize(MaximumDetailBytes - 3);
				detail += "...";
			}
			RouteDetail = std::move(detail);
		}

		UActor* FindReachSpecWaypoint(std::string& error)
		{
			ULevelInfo* levelInfo = ViewportPawn ? ViewportPawn->Level() : nullptr;
			ULevel* level = ViewportPawn ? ViewportPawn->XLevel() : nullptr;
			if (!levelInfo || !level)
			{
				error = "level navigation data is unavailable";
				return nullptr;
			}

			struct EndpointCandidate
			{
				UNavigationPoint* Actor = nullptr;
				double TargetDistance = 0.0;
				bool ExactInventorySpot = false;
				bool TargetVisible = false;
				std::string Identity;
			};

			RoutePlanRequest request;
			request.Origin = PlayerPosition();
			request.PawnRadius = ViewportPawn->CollisionRadius();
			request.PawnHeight = ViewportPawn->CollisionHeight();
			request.WaypointRadius = 32.0;
			const double originTargetDistance = length(
				ViewportPawn->Location() - TrackedTarget->Location());

			std::map<std::string, UNavigationPoint*> actorsByIdentity;
			UInventory* trackedInventory = UObject::TryCast<UInventory>(TrackedTarget);
			UInventorySpot* trackedInventoryMarker = trackedInventory ?
				trackedInventory->myMarker() : nullptr;
			std::vector<EndpointCandidate> candidates;
			std::set<std::string> candidateIdentities;
			std::vector<UNavigationPoint*> navigationPoints;
			for (UNavigationPoint* navPoint = levelInfo->NavigationPointList(); navPoint;
				navPoint = navPoint->nextNavigationPoint())
			{
				if (navPoint->bDeleteMe())
					continue;
				navigationPoints.push_back(navPoint);
				const std::string identity = ActorIdentity(navPoint);
				actorsByIdentity.emplace(identity, navPoint);
				const vec3 fromPlayer = navPoint->Location() - ViewportPawn->Location();
				RouteNode node;
				node.Identity = identity;
				node.Location = { navPoint->Location().x, navPoint->Location().y,
					navPoint->Location().z };
				node.DirectlyReachable = dot(fromPlayer, fromPlayer) <= 1000.0f * 1000.0f &&
					ViewportPawn->PointReachable(navPoint->Location());
				request.Nodes.push_back(std::move(node));

				UInventorySpot* inventorySpot = UObject::TryCast<UInventorySpot>(navPoint);
				const bool exactInventorySpot = inventorySpot &&
					(inventorySpot == trackedInventoryMarker ||
						inventorySpot->markedItem() == TrackedTarget);
				const vec3 targetDelta = navPoint->Location() - TrackedTarget->Location();
				const double targetDistance = length(targetDelta);
				vec3 navEye = navPoint->Location();
				navEye.z += ViewportPawn->BaseEyeHeight();
				const bool targetVisible = ViewportPawn->FastTrace(
					TrackedTarget->Location(), navEye);
				if (exactInventorySpot || (targetVisible && targetDistance <= 1000.0 &&
					targetDistance + 1.0 < originTargetDistance))
				{
					candidates.push_back({ navPoint, targetDistance, exactInventorySpot,
						targetVisible, identity });
					candidateIdentities.insert(identity);
				}
			}
			if (navigationPoints.empty())
			{
				error = "map exposes no navigation endpoint";
				return nullptr;
			}

			for (const LevelReachSpec& spec : level->ReachSpecs)
			{
				if (!spec.startActor || !spec.endActor || spec.startActor->bDeleteMe() ||
					spec.endActor->bDeleteMe())
					continue;
				const std::string fromIdentity = ActorIdentity(spec.startActor);
				const std::string toIdentity = ActorIdentity(spec.endActor);
				if (actorsByIdentity.find(fromIdentity) == actorsByIdentity.end() ||
					actorsByIdentity.find(toIdentity) == actorsByIdentity.end())
					continue;
				uint32_t distance = spec.distance > 0 ? static_cast<uint32_t>(spec.distance) : 1;
				request.Edges.push_back({ fromIdentity, toIdentity, distance,
					static_cast<double>(spec.collisionRadius),
					static_cast<double>(spec.collisionHeight), spec.bPruned != 0 });
			}

			for (const std::string& identity : VisitedWaypoints)
			{
				if (actorsByIdentity.find(identity) != actorsByIdentity.end())
					request.VisitedIdentities.push_back(identity);
			}

			for (UNavigationPoint* navPoint : navigationPoints)
			{
				const std::string identity = ActorIdentity(navPoint);
				if (candidateIdentities.find(identity) != candidateIdentities.end())
					continue;
				const vec3 delta = navPoint->Location() - TrackedTarget->Location();
				const double targetDistance = length(delta);
				if (targetDistance + 1.0 >= originTargetDistance)
					continue;
				candidates.push_back({ navPoint, targetDistance,
					false, false, identity });
			}
			std::sort(candidates.begin(), candidates.end(), [](const EndpointCandidate& left,
				const EndpointCandidate& right)
			{
				if (left.ExactInventorySpot != right.ExactInventorySpot)
					return left.ExactInventorySpot > right.ExactInventorySpot;
				if (left.TargetDistance != right.TargetDistance)
					return left.TargetDistance < right.TargetDistance;
				if (left.TargetVisible != right.TargetVisible)
					return left.TargetVisible > right.TargetVisible;
				return left.Identity < right.Identity;
			});
			if (candidates.size() > 32)
				candidates.resize(32);

			UNavigationPoint* bestWaypoint = nullptr;
			double bestScore = std::numeric_limits<double>::infinity();
			std::string bestEndpointIdentity;
			size_t maximumReachableStarts = 0;
			std::string lastError = "no endpoint candidate produced a route";
			for (const EndpointCandidate& candidate : candidates)
			{
				for (RouteNode& node : request.Nodes)
					node.Endpoint = node.Identity == candidate.Identity;
				const RoutePlanResult route = PlanReachSpecRoute(request);
				maximumReachableStarts = std::max(maximumReachableStarts,
					route.ReachableStartCount);
				if (!route)
				{
					lastError = route.Error;
					continue;
				}
				if (route.Waypoints.empty())
					continue;
				const double score = route.TotalDistance + candidate.TargetDistance;
				if (score > bestScore ||
					(score == bestScore && candidate.Identity >= bestEndpointIdentity))
					continue;
				const auto waypoint = actorsByIdentity.find(route.Waypoints.front().Identity);
				if (waypoint == actorsByIdentity.end())
					continue;
				bestScore = score;
				bestEndpointIdentity = candidate.Identity;
				bestWaypoint = waypoint->second;
			}
			if (bestWaypoint)
				return bestWaypoint;
			error = "reach-spec graph has " + std::to_string(request.Nodes.size()) +
				" nodes, " + std::to_string(request.Edges.size()) + " links, " +
				std::to_string(maximumReachableStarts) + " reachable starts, and " +
				std::to_string(candidates.size()) + " bounded endpoint candidates: " + lastError;
			return nullptr;
		}

		UActor* FindReachableSpatialWaypoint(std::string& error)
		{
			if (!ViewportPawn || !EngineRef.Level || !TrackedTarget)
			{
				error = "live spatial waypoint data is unavailable";
				return nullptr;
			}

			struct LiveCandidate
			{
				UActor* Actor = nullptr;
				RouteNode Node;
				double TargetDistance = 0.0;
			};

			const WorldPoint origin = PlayerPosition();
			const WorldPoint target = { TrackedTarget->Location().x,
				TrackedTarget->Location().y, TrackedTarget->Location().z };
			const double originTargetDistance = std::hypot(
				std::hypot(origin.X - target.X, origin.Y - target.Y),
				origin.Z - target.Z);
			std::vector<LiveCandidate> candidates;
			for (UActor* actor : EngineRef.Level->Actors)
			{
				if (!actor || actor == ViewportPawn || actor == TrackedTarget ||
					actor->bDeleteMe() || actor->Owner() ||
					actor->CollisionRadius() <= 0.0f || actor->CollisionHeight() <= 0.0f)
					continue;
				const vec3 fromPlayer = actor->Location() - ViewportPawn->Location();
				const double playerDistanceSquared = dot(fromPlayer, fromPlayer);
				if (!std::isfinite(playerDistanceSquared) || playerDistanceSquared > 1000.0 * 1000.0)
					continue;
				const vec3 toTarget = actor->Location() - TrackedTarget->Location();
				const double targetDistance = length(toTarget);
				if (!std::isfinite(targetDistance) || targetDistance + 1.0 >= originTargetDistance)
					continue;
				RouteNode node;
				node.Identity = ActorIdentity(actor);
				node.Location = { actor->Location().x, actor->Location().y,
					actor->Location().z };
				candidates.push_back({ actor, std::move(node), targetDistance });
			}
			std::sort(candidates.begin(), candidates.end(),
				[](const LiveCandidate& left, const LiveCandidate& right)
			{
				if (left.TargetDistance != right.TargetDistance)
					return left.TargetDistance < right.TargetDistance;
				return left.Node.Identity < right.Node.Identity;
			});
			if (candidates.size() > MaximumSpatialWaypointCandidates)
				candidates.resize(MaximumSpatialWaypointCandidates);
			if (candidates.empty())
			{
				error = "no bounded collidable actor makes target progress";
				return nullptr;
			}

			SpatialWaypointRequest request;
			request.Origin = origin;
			request.Target = target;
			request.ArrivalRadius = 32.0;
			request.MinimumProgress = 1.0;
			std::map<std::string, UActor*> actorsByIdentity;
			for (LiveCandidate& candidate : candidates)
			{
				candidate.Node.DirectlyReachable =
					ViewportPawn->PointReachable(candidate.Actor->Location());
				actorsByIdentity.emplace(candidate.Node.Identity, candidate.Actor);
				request.Candidates.push_back(std::move(candidate.Node));
			}
			for (const std::string& identity : VisitedWaypoints)
				request.VisitedIdentities.push_back(identity);

			const SpatialWaypointResult selected =
				SelectReachableSpatialWaypoint(request);
			if (!selected)
			{
				error = selected.Error;
				return nullptr;
			}
			const auto found = actorsByIdentity.find(selected.Waypoint.Identity);
			if (found == actorsByIdentity.end())
			{
				error = "selected spatial waypoint has no live actor binding";
				return nullptr;
			}
			return found->second;
		}

		ObservationSnapshot CaptureObservation(uint64_t revision, uint64_t tick,
			bool actionDeltaOnly) const
		{
			struct Candidate
			{
				UActor* Actor = nullptr;
				int Priority = 0;
				double DistanceSquared = 0.0;
				std::string Identity;
			};

			ObservationSnapshot observation;
			observation.Revision = revision;
			observation.Tick = tick;
			observation.PlayerIdentity = ActorIdentity(ViewportPawn);
			observation.PlayerPosition = PlayerPosition();
			if (!ViewportPawn || !EngineRef.Level)
				return observation;

			UDeusExPlayer* deusExPlayer = UObject::TryCast<UDeusExPlayer>(ViewportPawn);
			UActor* frobTarget = deusExPlayer ? deusExPlayer->FrobTarget() : nullptr;
			std::vector<Candidate> candidates;
			bool trackedTargetSeen = false;
			auto appendCandidate = [&](UActor* actor)
			{
				if (!actor || actor == ViewportPawn)
					return;
				const bool tracked = actor == TrackedTarget;
				const bool interactionWaypoint = actor == InteractionWaypoint;
				if (actor->bDeleteMe() && !tracked)
					return;
				trackedTargetSeen = trackedTargetSeen || tracked;
				const double deltaX = actor->Location().x - ViewportPawn->Location().x;
				const double deltaY = actor->Location().y - ViewportPawn->Location().y;
				const double deltaZ = actor->Location().z - ViewportPawn->Location().z;
				const double distanceSquared = deltaX * deltaX + deltaY * deltaY + deltaZ * deltaZ;
				if (!std::isfinite(distanceSquared))
					return;
				const bool ownedByPlayer = actor->Owner() == ViewportPawn;
				const bool inventory = UObject::TryCast<UInventory>(actor) != nullptr;
				const bool eventReceiver =
					(TrackedTarget && !TrackedTarget->Event().IsNone() &&
						actor->Tag() == TrackedTarget->Event()) ||
					(InteractionWaypoint && !InteractionWaypoint->Event().IsNone() &&
						actor->Tag() == InteractionWaypoint->Event());
				if (actionDeltaOnly && !tracked && !interactionWaypoint && actor != frobTarget &&
					!eventReceiver && !(ownedByPlayer && inventory))
					return;
				const int priority = tracked ? 0 : interactionWaypoint ? 1 :
					actor == frobTarget ? 2 : eventReceiver ? 3 :
					(ownedByPlayer && inventory) ? 4 : inventory ? 5 : 6;
				if (priority == 6 && distanceSquared > 4096.0 * 4096.0)
					return;
				candidates.push_back({ actor, priority, distanceSquared, ActorIdentity(actor) });
			};
			for (UActor* actor : EngineRef.Level->Actors)
				appendCandidate(actor);
			if (TrackedTarget && !trackedTargetSeen)
				appendCandidate(TrackedTarget);

			std::sort(candidates.begin(), candidates.end(), [](const Candidate& left, const Candidate& right)
			{
				if (left.Priority != right.Priority)
					return left.Priority < right.Priority;
				if (left.DistanceSquared != right.DistanceSquared)
					return left.DistanceSquared < right.DistanceSquared;
				return left.Identity < right.Identity;
			});
			if (candidates.size() > MaximumObservationTargets)
				candidates.resize(MaximumObservationTargets);

			observation.Targets.reserve(candidates.size());
			for (const Candidate& candidate : candidates)
			{
				UActor* actor = candidate.Actor;
				const bool deleted = actor->bDeleteMe();
				TargetSnapshot target;
				target.ObservationRevision = revision;
				target.Identity = candidate.Identity;
				target.ClassName = UObject::GetUClassFullName(actor).ToString();
				target.OwnerIdentity = ActorIdentity(actor->Owner());
				target.StateToken = actor->GetStateName().ToString();
				target.TagName = actor->Tag().IsNone() ? std::string() : actor->Tag().ToString();
				target.EventName = actor->Event().IsNone() ? std::string() : actor->Event().ToString();
				if (actor->HasProperty("AmmoAmount"))
				{
					target.InventoryResource = TargetSnapshot::InventoryResourceSnapshot{
						TargetSnapshot::InventoryResourceKind::AmmoAmount,
						actor->GetInt("AmmoAmount") };
				}
				else if (actor->HasProperty("NumCopies"))
				{
					target.InventoryResource = TargetSnapshot::InventoryResourceSnapshot{
						TargetSnapshot::InventoryResourceKind::NumCopies,
						actor->GetInt("NumCopies") };
				}
				target.Location = { actor->Location().x, actor->Location().y, actor->Location().z };
				target.Deleted = deleted;
				target.Reachable = !actionDeltaOnly && !deleted && actor->Owner() == nullptr &&
					ViewportPawn->PointReachable(actor->Location());
				target.Interactable = !deleted && actor == frobTarget;
				target.Acquirable = !deleted && actor->Owner() == nullptr &&
					UObject::TryCast<UInventory>(actor) != nullptr;
				observation.Targets.push_back(std::move(target));
			}
			return observation;
		}

		ObservationSnapshot CaptureMinimalObservation(uint64_t revision, uint64_t tick) const
		{
			ObservationSnapshot observation;
			observation.Revision = revision;
			observation.Tick = tick;
			observation.PlayerIdentity = ActorIdentity(ViewportPawn);
			observation.PlayerPosition = PlayerPosition();
			return observation;
		}

		void WriteObservation(const ObservationSnapshot& observation,
			const char* filename = "observation.json")
		{
			const std::filesystem::path observationPath =
				std::filesystem::path(Config.GetOutputDirectory()) / filename;
			File::write_all_text(observationPath.string(), ObservationSnapshotJson(observation));
			LogMessage("Player automation observation: " + observationPath.string() +
				" (" + ObservationSnapshotDigest(observation) + ")");
		}

		void AdvanceLevel(const DeterministicFrameTime& frameTime)
		{
			const float levelElapsed = frameTime.RealElapsed *
				clamp(EngineRef.LevelInfo->TimeDilation(), 0.0025f, 25.0f);
			EngineRef.TotalTime = frameTime.TotalReal;
			EngineRef.LevelInfo->TimeSeconds() += levelElapsed;
			Logger::Get()->SetTimeSeconds(EngineRef.LevelInfo->TimeSeconds());

			const RuntimeWallClock& wallClock = GetConfig().Runtime.WallClock;
			EngineRef.LevelInfo->Year() = wallClock.Year;
			EngineRef.LevelInfo->Month() = wallClock.Month;
			EngineRef.LevelInfo->Day() = wallClock.Day;
			EngineRef.LevelInfo->DayOfWeek() = wallClock.DayOfWeek;
			EngineRef.LevelInfo->Hour() = wallClock.Hour;
			EngineRef.LevelInfo->Minute() = wallClock.Minute;
			EngineRef.LevelInfo->Second() = wallClock.Second;
			EngineRef.LevelInfo->Millisecond() = wallClock.Millisecond;

			EngineRef.SetPause(false);
			CallEvent(EngineRef.console, EventName::Tick,
				{ ExpressionValue::FloatValue(levelElapsed) });
			if (EngineRef.LaunchInfo.ue1Version >= 436)
			{
				EngineRef.LevelInfo->bDropDetail() = false;
				EngineRef.LevelInfo->bAggressiveLOD() = false;
			}
			EngineRef.Level->Tick(levelElapsed, false);

			const std::optional<AutomationCaptureRequest>& request =
				Config.GetCaptureRequest();
			if (request && request->Phase == AutomationCapturePhase::PostSimulation &&
				!CaptureCompleted && frameTime.Tick == request->Tick)
			{
				(void)RefreshStockFrobTarget();
				EngineRef.CaptureAutomationFrame(Config.GetOutputDirectory(),
					request->SessionId, request->SourceRevision, request->SourceDirty,
					ActiveCommand.Id, Config.ConfigIdentity(), frameTime.Tick,
					ObservationRevision, AutomationCapturePhaseName(request->Phase), 0,
					[&]()
					{
						return ObservationSnapshotJson(CaptureObservation(
							ObservationRevision, frameTime.Tick, false));
					});
				CaptureCompleted = true;
			}
		}

		static const TargetSnapshot* FindSnapshot(const ObservationSnapshot& observation,
			const std::string& identity)
		{
			const TargetSnapshot* match = nullptr;
			for (const TargetSnapshot& target : observation.Targets)
			{
				if (target.Identity != identity)
					continue;
				if (match)
					throw std::runtime_error(
						"capture barrier target identity became ambiguous");
				match = &target;
			}
			return match;
		}

		static bool SameResource(const std::optional<TargetSnapshot::InventoryResourceSnapshot>& a,
			const std::optional<TargetSnapshot::InventoryResourceSnapshot>& b)
		{
			return (!a && !b) || (a && b && a->Kind == b->Kind && a->Value == b->Value);
		}

		static bool SameProofState(const TargetSnapshot& a, const TargetSnapshot& b)
		{
			return a.Identity == b.Identity && a.ClassName == b.ClassName &&
				a.OwnerIdentity == b.OwnerIdentity && a.StateToken == b.StateToken &&
				a.TagName == b.TagName && a.EventName == b.EventName &&
				SameResource(a.InventoryResource, b.InventoryResource) &&
				a.Location.X == b.Location.X && a.Location.Y == b.Location.Y &&
				a.Location.Z == b.Location.Z && a.Deleted == b.Deleted &&
				a.Interactable == b.Interactable && a.Acquirable == b.Acquirable;
		}

		void ValidateBarrierObservation(const ObservationSnapshot& before,
			const ObservationSnapshot& after) const
		{
			if (before.Revision != after.Revision || before.Tick != after.Tick ||
				before.PlayerIdentity != after.PlayerIdentity ||
				before.PlayerPosition.X != after.PlayerPosition.X ||
				before.PlayerPosition.Y != after.PlayerPosition.Y ||
				before.PlayerPosition.Z != after.PlayerPosition.Z ||
				!ActiveCommand.Target)
				throw std::runtime_error(
					"capture barrier changed the player observation boundary");
			const TargetSnapshot* original = FindSnapshot(before,
				ActiveCommand.Target->Identity);
			const TargetSnapshot* captured = FindSnapshot(after,
				ActiveCommand.Target->Identity);
			if (!original || !captured || !SameProofState(*original, *captured))
				throw std::runtime_error(
					"capture barrier changed the exact target proof state");
			if (Config.GetCaptureRequest()->Phase ==
				AutomationCapturePhase::PreStockInteraction && !captured->Interactable)
				throw std::runtime_error(
					"pre-stock-interaction capture lost the exact stock FrobTarget");
			if (Config.GetCaptureRequest()->Phase == AutomationCapturePhase::PreAction &&
				(!original->Reachable || !captured->Reachable))
				throw std::runtime_error(
					"pre-action capture lost the exact reachable walk target");
			if (Config.GetCaptureRequest()->Phase == AutomationCapturePhase::PrePickup &&
				(ActiveCommand.Kind != CommandKind::AcquireItem || original->Deleted ||
				 captured->Deleted || !original->Interactable || !captured->Interactable ||
				 !original->Acquirable || !captured->Acquirable ||
				 !original->OwnerIdentity.empty() || !captured->OwnerIdentity.empty()))
				throw std::runtime_error(
					"pre-pickup capture lost the exact unowned acquirable target");
			if (!original->EventName.empty())
			{
				for (const TargetSnapshot& receiver : before.Targets)
				{
					if (receiver.TagName != original->EventName)
						continue;
					const TargetSnapshot* afterReceiver = FindSnapshot(after,
						receiver.Identity);
					if (!afterReceiver || !SameProofState(receiver, *afterReceiver))
						throw std::runtime_error(
							"capture barrier changed an exact interaction receiver");
				}
			}
		}

		void CaptureActionBarrier(const ObservationSnapshot& before, uint64_t tick)
		{
			const AutomationCaptureRequest& request = *Config.GetCaptureRequest();
			if (CaptureCompleted || request.Tick != tick)
				throw std::runtime_error(
					"action capture barrier did not occur at its asserted tick");
			ObservationSnapshot captured;
			const uint64_t eventSequence = TelemetryEventCount + 1;
			EngineRef.CaptureAutomationFrame(Config.GetOutputDirectory(),
				request.SessionId, request.SourceRevision, request.SourceDirty,
				ActiveCommand.Id, Config.ConfigIdentity(), tick,
				ObservationRevision, AutomationCapturePhaseName(request.Phase),
				eventSequence, [&]()
				{
					captured = CaptureObservation(ObservationRevision, tick,
						request.Phase == AutomationCapturePhase::PreStockInteraction ||
						request.Phase == AutomationCapturePhase::PrePickup);
					ValidateBarrierObservation(before, captured);
					return ObservationSnapshotJson(captured);
				});
			CaptureCompleted = true;
			WriteEvent(TelemetryEventType::VisualCapturePublished, tick,
				ObservationRevision, CommandState::Running,
				ActiveCommand.Target->Identity, PlayerPosition(), {},
				"engine-owned " + std::string(AutomationCapturePhaseName(request.Phase)) +
					" visual capture published before the next synthetic action input");
			if (TelemetryEventCount != eventSequence)
				throw std::runtime_error(
					"capture barrier telemetry sequence did not match its receipt");
		}

		bool RefreshStockFrobTarget()
		{
			static const NameString selectionFunction("HighlightCenterObject");
			if (!ViewportPawn || !FindEventFunction(ViewportPawn, selectionFunction))
				return false;
			CallEvent(ViewportPawn, selectionFunction);
			return true;
		}

		WorldPoint PlayerPosition() const
		{
			if (!ViewportPawn)
				return {};
			return { ViewportPawn->Location().x, ViewportPawn->Location().y,
				ViewportPawn->Location().z };
		}

		std::string TrackedTargetDiagnostics() const
		{
			if (!ViewportPawn || !TrackedTarget)
				return "target diagnostics unavailable";
			std::string detail = "tag=" + TrackedTarget->Tag().ToString() +
				" event=" + TrackedTarget->Event().ToString() +
				" collision_radius=" + std::to_string(TrackedTarget->CollisionRadius()) +
				" collision_height=" + std::to_string(TrackedTarget->CollisionHeight());
			if (TrackedTarget->HasProperty("bHighlight"))
				detail += " bHighlight=" +
					std::string(TrackedTarget->GetBool("bHighlight") ? "true" : "false");
			if (UDeusExPlayer* player = UObject::TryCast<UDeusExPlayer>(ViewportPawn))
				detail += " max_frob_distance=" + std::to_string(player->MaxFrobDistance());
			return detail;
		}

		AimObservation CaptureAimObservation() const
		{
			AimObservation observation;
			if (!ViewportPawn)
				return observation;
			observation.EyePosition = PlayerPosition();
			observation.EyePosition.Z += ViewportPawn->EyeHeight();
			const Rotator viewRotation = normalize(ViewportPawn->ViewRotation());
			observation.ViewYawRadians = viewRotation.YawRadians();
			observation.ViewPitchRadians = viewRotation.PitchRadians();
			return observation;
		}

		void WriteEvent(TelemetryEventType type, uint64_t tick,
			uint64_t observationRevision, std::optional<CommandState> state,
			std::string targetIdentity, std::optional<WorldPoint> position,
			std::optional<double> distance, std::string detail,
			const AutomationCommand* eventCommand = nullptr)
		{
			if (!TelemetryFile)
				return;
			const size_t routeAllowance = MaximumRouteSegments *
				(Config.HasFollowupCommand() ? 2 : 1);
			if (TelemetryEventCount >= Config.GetMaxTicks() + routeAllowance + 16)
				throw std::runtime_error("player automation telemetry event cap reached");
			TelemetryEvent event;
			event.Sequence = ++TelemetryEventCount;
			event.Tick = tick;
			event.ObservationRevision = observationRevision;
			event.ConfigIdentity = Config.ConfigIdentity();
			event.Type = type;
			const AutomationCommand& command = eventCommand ?
				*eventCommand : ActiveCommand;
			event.CommandId = command.Id;
			event.Kind = command.Kind;
			event.State = state;
			event.TargetIdentity = std::move(targetIdentity);
			event.Detail = std::move(detail);
			event.Position = position;
			event.Distance = distance;
			const std::string line = TelemetryEventJson(event);
			TelemetryFile->write(line.data(), line.size());
		}

		CommandResult BuildResult(CommandState state, uint64_t tick,
			uint64_t observationRevision, std::string reason) const
		{
			CommandResult result;
			result.CommandId = ActiveCommand.Id;
			result.Kind = ActiveCommand.Kind;
			result.State = state;
			result.Tick = tick;
			result.ObservationRevision = observationRevision;
			result.TargetIdentity = ActiveCommand.Target ?
				ActiveCommand.Target->Identity :
				Config.GetSightProbeTarget() ?
					Config.GetSightProbeTarget()->Identity : std::string();
			for (char& character : reason)
			{
				const unsigned char value = static_cast<unsigned char>(character);
				if (value < 0x20 || value > 0x7e)
					character = '?';
			}
			if (reason.size() > MaximumDetailBytes)
				reason.resize(MaximumDetailBytes);
			if (reason.empty() && state != CommandState::Succeeded)
				reason = "automation command failed without a diagnostic";
			result.Reason = std::move(reason);
			return result;
		}

		std::string SequenceSummaryJson() const
		{
			std::ostringstream out;
			out << "{\n"
				<< "  \"schema\": \"surreal-player-automation-sequence-result-v1\",\n"
				<< "  \"config_identity\": \"" << Config.ConfigIdentity() << "\",\n"
				<< "  \"state\": \"" << CommandStateName(Result.State) << "\",\n"
				<< "  \"expected_commands\": \"2\",\n"
				<< "  \"completed_commands\": \"" << Results.size() << "\",\n"
				<< "  \"results\": [";
			for (size_t index = 0; index < Results.size(); index++)
			{
				if (index != 0)
					out << ',';
				std::string resultJson = CommandResultJson(Results[index]);
				if (!resultJson.empty() && resultJson.back() == '\n')
					resultJson.pop_back();
				out << '\n' << "    " << resultJson;
			}
			if (!Results.empty())
				out << "  ";
			out << "]\n}\n";
			return out.str();
		}

		void Finalize(CommandState state, uint64_t tick,
			uint64_t observationRevision, std::string reason)
		{
			if (Complete)
				return;
			Input.Release(EngineRef);
			AimInput.Release(EngineRef);
			WaypointAction.Reset();
			IntermediateReceiverBaselines.clear();
			ScriptedMoverBaselines.clear();
			MovementMoverBaseline.reset();
			PendingVacatedPassage.reset();
			ScriptedObstacleWaitDeadline = 0;
			InteractionWaypoint = nullptr;
			MovementWaypoint = nullptr;
			Result = BuildResult(state, tick, observationRevision, std::move(reason));
			Results.push_back(Result);
			Complete = true;
			try
			{
				WriteEvent(TelemetryEventType::CommandResult, tick, observationRevision,
					state, Result.TargetIdentity, PlayerPosition(), {}, Result.Reason);
			}
			catch (const std::exception& e)
			{
				LogMessage("Player automation terminal telemetry failed: " + std::string(e.what()));
			}
		}

		Engine& EngineRef;
		const PlayerAutomationRunConfig Config;
		AutomationCommand ActiveCommand;
		PlayerMovementController Controller;
		PlayerMovementInputAdapter Input;
		PlayerAimController InteractionAim;
		PlayerAimInputAdapter AimInput;
		InteractionActionController Action;
		InteractionActionController WaypointAction;
		UPlayerPawn* ViewportPawn = nullptr;
		UActor* TrackedTarget = nullptr;
		UActor* MovementWaypoint = nullptr;
		UActor* InteractionWaypoint = nullptr;
		std::vector<IntermediateReceiverBaseline> IntermediateReceiverBaselines;
		std::vector<IntermediateReceiverBaseline> ScriptedMoverBaselines;
		std::optional<IntermediateReceiverBaseline> MovementMoverBaseline;
		std::optional<WorldPoint> PendingVacatedPassage;
		uint64_t ScriptedObstacleWaitDeadline = 0;
		bool ScriptedObstacleWaitUsed = false;
		TargetSnapshot TrackedTargetSnapshot;
		std::set<std::string> VisitedWaypoints;
		size_t RouteSegmentCount = 0;
		bool MovingToFinalTarget = false;
		bool RoutePlanningPending = false;
		std::string RouteDetail;
		std::shared_ptr<File> TelemetryFile;
		uint64_t TelemetryEventCount = 0;
		uint64_t ObservationRevision = 1;
		uint64_t Ticks = 0;
		bool Complete = false;
		bool FollowupStarted = false;
		bool CaptureCompleted = false;
		bool CaptureFirstEffectCompleted = false;
		CommandResult Result;
		std::vector<CommandResult> Results;
		std::optional<ObservationSnapshot> SightProbeObservation;
		mutable std::map<UActor*, std::string> ActorIdentities;
		mutable std::map<std::string, size_t> NextActorOrdinal;
	};

	PlayerAutomationRunConfig ConfigFromCommandLine()
	{
		return PlayerAutomationRunConfig::Parse(
			commandline ? commandline->GetArg("", "--automation-url") : std::string(),
			commandline ? commandline->GetArg("", "--automation-output") : std::string(),
			commandline ? commandline->GetArg("", "--automation-target-x") : std::string(),
			commandline ? commandline->GetArg("", "--automation-target-y") : std::string(),
			commandline ? commandline->GetArg("", "--automation-target-z") : std::string(),
			commandline ? commandline->GetArg("", "--automation-arrival-radius") : std::string(),
			commandline ? commandline->GetArg("", "--automation-seed") : std::string(),
			commandline ? commandline->GetArg("", "--automation-ticks") : std::string(),
			commandline ? commandline->GetArg("", "--automation-fixed-delta") : std::string(),
			commandline ? commandline->GetArg("", "--automation-action") : std::string(),
			commandline ? commandline->GetArg("", "--automation-wait-ticks") : std::string(),
			commandline ? commandline->GetArg("", "--automation-target-identity") : std::string(),
			commandline ? commandline->GetArg("", "--automation-target-class") : std::string(),
			commandline ? commandline->GetArg("", "--automation-abort-at-tick") : std::string(),
			commandline ? commandline->GetArg("", "--automation-followup-target-identity") : std::string(),
			commandline ? commandline->GetArg("", "--automation-capture-tick") : std::string(),
			commandline ? commandline->GetArg("", "--automation-capture-session") : std::string(),
			commandline ? commandline->GetArg("", "--automation-capture-source-revision") : std::string(),
			commandline ? commandline->GetArg("", "--automation-capture-source-dirty") : std::string(),
			commandline ? commandline->GetArg("", "--automation-capture-phase") : std::string());
	}
}

void RegisterPlayerAutomationDriver(HeadlessDriverRegistry& registry)
{
	if (!registry.Contains("player-automation"))
	{
		registry.Register("player-automation", [](Engine& engine)
		{
			return std::make_unique<PlayerAutomationDriver>(engine, ConfigFromCommandLine());
		});
	}
}
