#include "Precomp.h"
#include "DockConversationDriver.h"
#include "Audio/NullAudioDevice.h"
#include "Engine.h"
#include "Package/PackageManager.h"
#include "Runtime/HeadlessDriver.h"
#include "Utils/CommandLine.h"
#include "Utils/File.h"
#include "Utils/Logger.h"
#include "UObject/UActor.h"
#include "UObject/UClient.h"
#include "UObject/ULevel.h"
#include "UObject/USubsystem.h"
#include "UObject/UWindow.h"
#include "VM/ScriptCall.h"

#include <filesystem>

namespace
{
	class HeadlessConversationAudioDevice final : public NullAudioDevice
	{
	public:
		int GetTotalChannels() override { return 256; }
	};

	class DeusExDockConversationDriver final : public HeadlessDriver
	{
	public:
		DeusExDockConversationDriver(Engine& engine, std::string outputDirectory)
			: EngineRef(engine), OutputDirectory(std::move(outputDirectory))
		{
		}

		HeadlessDriverConfig GetConfig() const override
		{
			HeadlessDriverConfig config;
			config.Runtime.Seed = 1;
			config.Runtime.FixedDelta = 1.0f / 60.0f;
			config.MaxTicks = 6000;
			return config;
		}

		void Start() override
		{
			try
			{
				if (!EngineRef.LaunchInfo.IsDeusEx())
					return Fail("the selected game is not Deus Ex");

				EngineRef.audiodev->InitDevice(std::make_unique<HeadlessConversationAudioDevice>());
				EngineRef.viewport->SetViewportRect(0, 0, 1024, 768);
				EngineRef.LaunchInfo.noEntryMap = true;
				EngineRef.LaunchInfo.url = "01_NYC_UNATCOIsland";
				UnrealURL url(EngineRef.GetDefaultURL(
					EngineRef.packages->GetIniValue("system", "URL", "LocalMap")),
					EngineRef.LaunchInfo.url);
				EngineRef.LoadMap(url);
				EngineRef.LoginPlayer();
			}
			catch (const std::exception& e)
			{
				Fail(e.what());
			}
		}

		bool IsComplete() const override { return Complete; }

		void Tick(const DeterministicFrameTime& frameTime) override
		{
			try
			{
				CurrentTick = frameTime.Tick;
				const float elapsed = frameTime.RealElapsed * clamp(
					EngineRef.LevelInfo->TimeDilation(), 0.0025f, 25.0f);
				EngineRef.TotalTime = frameTime.TotalReal;
				EngineRef.LevelInfo->TimeSeconds() += elapsed;
				Logger::Get()->SetTimeSeconds(EngineRef.LevelInfo->TimeSeconds());
				EngineRef.SetPause(false);
				CallEvent(EngineRef.console, EventName::Tick, { ExpressionValue::FloatValue(elapsed) });
				EngineRef.Level->Tick(elapsed, false);
				if (EngineRef.dxRootWindow)
				{
					EngineRef.dxRootWindow->Tick(elapsed);
					EngineRef.dxRootWindow->UpdateLayout();
				}
				EngineRef.UpdateAudio();

				if (!InteractionStarted && frameTime.TotalReal >= 2.0)
					StartConversation();
				if (InteractionStarted && !ChoicePressed)
					PressChoice();
				else if (ChoicePressed && !ChoiceReleased && CurrentTick >= ReleaseTick)
					ReleaseChoice();
				else if (ChoiceReleased && CurrentTick >= ResultTick)
					CheckResult();

				if (!Complete && frameTime.TotalReal >= 90.0)
					Fail("timed out waiting for the Paul Denton conversation choice");
			}
			catch (const std::exception& e)
			{
				Fail(e.what());
			}
		}

		int Finish(const HeadlessRunSummary& summary) override
		{
			if (summary.TickLimitReached && ExitCode == 0)
				Fail("headless tick limit reached before the conversation completed");

			std::filesystem::create_directories(OutputDirectory);
			const std::filesystem::path resultPath = std::filesystem::path(OutputDirectory) / "result.json";
			const std::string status = ExitCode == 0 ? "pass" : "fail";
			const std::string json =
				"{\n"
				"  \"status\": \"" + status + "\",\n"
				"  \"game\": \"" + EscapeJson(EngineRef.LaunchInfo.gameName) + "\",\n"
				"  \"version\": \"" + EscapeJson(EngineRef.LaunchInfo.gameVersionString) + "\",\n"
				"  \"map\": \"01_NYC_UNATCOIsland\",\n"
				"  \"choicePresented\": " + std::string(ChoicePressed ? "true" : "false") + ",\n"
				"  \"choiceWindow\": \"" + EscapeJson(ChoiceWindowName) + "\",\n"
				"  \"focusedWindow\": \"" + EscapeJson(FocusedWindowClass) + "\",\n"
				"  \"rawTileSynthesized\": " + std::string(RawTileSynthesized ? "true" : "false") + ",\n"
				"  \"rawPressSuppressed\": " + std::string(RawPressSuppressed ? "true" : "false") + ",\n"
				"  \"nativePressed\": " + std::string(NativePressed ? "true" : "false") + ",\n"
				"  \"choiceAdvanced\": " + std::string(ChoiceAdvanced ? "true" : "false") + ",\n"
				"  \"finalConversationState\": \"" + EscapeJson(FinalConversationState) + "\",\n"
				"  \"failureReason\": \"" + EscapeJson(FailureReason) + "\"\n"
				"}\n";
			File::write_all_text(resultPath.string(), json);
			return ExitCode;
		}

	private:
		static std::string EscapeJson(const std::string& value)
		{
			std::string result;
			for (char c : value)
			{
				if (c == '\\' || c == '"')
					result += '\\';
				result += c;
			}
			return result;
		}

		void Fail(std::string reason)
		{
			if (ExitCode == 0)
			{
				ExitCode = 1;
				FailureReason = std::move(reason);
			}
			Complete = true;
		}

		UWindow* FindVisibleChoice(UWindow* window) const
		{
			if (!window)
				return nullptr;
			if (window->bIsVisible() && UObject::GetUClassFullName(window) == "DeusEx.ConChoiceWindow")
				return window;
			for (UWindow* child = window->firstChild(); child; child = child->nextSibling())
				if (UWindow* choice = FindVisibleChoice(child))
					return choice;
			return nullptr;
		}

		UWindow* FindVisibleTile(UWindow* window) const
		{
			if (!window)
				return nullptr;
			const std::string className = UObject::GetUClassFullName(window).ToString();
			if (window->bIsVisible() && className.find("TileWindow") != std::string::npos)
				return window;
			for (UWindow* child = window->lastChild(); child; child = child->prevSibling())
				if (UWindow* tile = FindVisibleTile(child))
					return tile;
			return nullptr;
		}

		void StartConversation()
		{
			UDeusExPlayer* player = UObject::TryCast<UDeusExPlayer>(
				EngineRef.viewport ? EngineRef.viewport->Actor() : nullptr);
			UActor* paul = UObject::TryCast<UActor>(
				EngineRef.FindObject("PaulDenton0", "DeusEx.PaulDenton"));
			if (!player || !paul)
				return Fail("could not find the player and PaulDenton0");

			player->Location() = paul->Location() + vec3(96.0f, 0.0f, 0.0f);
			player->ViewRotation() = Rotator::FromVector(
				normalize(paul->Location() - player->Location()));
			player->Rotation() = player->ViewRotation();
			player->FrobTarget() = paul;
			CallEvent(player, "DoFrob",
				{ ExpressionValue::ObjectValue(player), ExpressionValue::ObjectValue(nullptr) });
			InteractionStarted = true;
		}

		void PressChoice()
		{
			UWindow* choice = FindVisibleChoice(EngineRef.dxRootWindow);
			if (!choice)
				return;
			UWindow* clickTarget = FindVisibleTile(choice);
			if (!clickTarget)
			{
				UClass* tileClass = EngineRef.packages->GetPackage("Extension")->GetClass("TileWindow");
				clickTarget = UObject::Cast<UWindow>(choice->NewChild(tileClass, true));
				clickTarget->SetPos(0.0f, 0.0f);
				clickTarget->SetSize(choice->Width(), choice->Height());
				clickTarget->UpdateLayout();
				RawTileSynthesized = true;
			}

			float x = clickTarget->Width() * 0.5f;
			float y = clickTarget->Height() * 0.5f;
			for (UWindow* window = clickTarget; window && window != EngineRef.dxRootWindow; window = window->parentOwner())
			{
				x += window->UsedX;
				y += window->UsedY;
			}
			EngineRef.dxRootWindow->SetRootCursorPos(x, y);
			float relativeX = 0.0f;
			float relativeY = 0.0f;
			UWindow* focus = EngineRef.dxRootWindow->GetCursorFocus(relativeX, relativeY);
			ChoiceWindowName = choice->Name.ToString();
			FocusedWindowClass = focus ? UObject::GetUClassFullName(focus).ToString() : "None";
			if (FocusedWindowClass.find("TileWindow") == std::string::npos)
				return Fail("the raw-input TileWindow was not under the synthetic cursor");
			EngineRef.dxRootWindow->OnWindowMouseDown({}, IK_LeftMouse);
			if (UButtonWindow* button = UObject::TryCast<UButtonWindow>(choice))
			{
				// A rendered Deus Ex choice places a raw-input TileWindow over this
				// button. The headless fixture supplies that tile and mirrors its
				// consumed press so the release cannot use normal button state.
				if (button->bMousePressed())
				{
					button->bMousePressed() = false;
					button->bButtonPressed() = false;
					if (EngineRef.dxRootWindow->grabbedWindow() == button)
						EngineRef.dxRootWindow->grabbedWindow() = nullptr;
					RawPressSuppressed = true;
				}
				NativePressed = button->bMousePressed();
			}
			ChoicePressed = true;
			ReleaseTick = CurrentTick + 2;
		}

		void ReleaseChoice()
		{
			EngineRef.dxRootWindow->OnWindowMouseUp({}, IK_LeftMouse);
			ChoiceReleased = true;
			ResultTick = CurrentTick + 10;
		}

		void CheckResult()
		{
			UDeusExPlayer* player = UObject::TryCast<UDeusExPlayer>(
				EngineRef.viewport ? EngineRef.viewport->Actor() : nullptr);
			UObject* conPlay = player && player->HasProperty("ConPlay") ? player->GetUObject("ConPlay") : nullptr;
			FinalConversationState = conPlay ? conPlay->GetStateName().ToString() : "None";
			ChoiceAdvanced = FindVisibleChoice(EngineRef.dxRootWindow) == nullptr &&
				FinalConversationState != "WaitForInput";
			if (!ChoiceAdvanced)
				return Fail("the conversation choice remained visible or stayed in WaitForInput");
			Complete = true;
		}

		Engine& EngineRef;
		std::string OutputDirectory;
		uint64_t CurrentTick = 0;
		uint64_t ReleaseTick = 0;
		uint64_t ResultTick = 0;
		int ExitCode = 0;
		bool Complete = false;
		bool InteractionStarted = false;
		bool ChoicePressed = false;
		bool ChoiceReleased = false;
		bool ChoiceAdvanced = false;
		bool NativePressed = false;
		bool RawTileSynthesized = false;
		bool RawPressSuppressed = false;
		std::string ChoiceWindowName = "None";
		std::string FocusedWindowClass = "None";
		std::string FinalConversationState = "None";
		std::string FailureReason;
	};
}

void RegisterDeusExDockConversationDriver(HeadlessDriverRegistry& registry)
{
	if (!registry.Contains("deus-ex-dock-conversation"))
	{
		registry.Register("deus-ex-dock-conversation", [](Engine& engine)
		{
			std::string output = commandline ? commandline->GetArg("", "--dx-dock-output") : std::string();
			if (output.empty())
				output = "deus-ex-dock-conversation";
			return std::make_unique<DeusExDockConversationDriver>(engine, std::move(output));
		});
	}
}
