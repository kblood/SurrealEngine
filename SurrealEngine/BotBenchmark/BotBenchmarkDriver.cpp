#include "Precomp.h"
#include "BotBenchmarkDriver.h"
#include "BotBenchmarkProtocol.h"
#include "Engine.h"
#include "Runtime/HeadlessDriver.h"
#include "Utils/CommandLine.h"
#include "Utils/File.h"
#include "Utils/Logger.h"
#include "Package/PackageManager.h"
#include "UObject/ULevel.h"
#include "UObject/UClient.h"
#include "VM/ScriptCall.h"

#include <filesystem>
#include <set>

namespace
{
	class BotBenchmarkDriver final : public HeadlessDriver
	{
	public:
		BotBenchmarkDriver(Engine& engine, BotBenchmarkRunConfig config)
			: EngineRef(engine), Config(std::move(config))
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
				SetupMapAndPlayer();
				if (!Complete)
					SetupBot();
			}
			catch (const std::exception& e)
			{
				Fail(e.what());
			}
		}

		bool IsComplete() const override
		{
			return Complete || Ticks >= Config.GetMaxTicks();
		}

		void Tick(const DeterministicFrameTime& frameTime) override
		{
			try
			{
				const float levelElapsed = frameTime.RealElapsed * clamp(EngineRef.LevelInfo->TimeDilation(), 0.0025f, 25.0f);
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
				CallEvent(EngineRef.console, EventName::Tick, { ExpressionValue::FloatValue(levelElapsed) });
				if (EngineRef.LaunchInfo.ue1Version >= 436)
				{
					EngineRef.LevelInfo->bDropDetail() = false;
					EngineRef.LevelInfo->bAggressiveLOD() = false;
				}
				EngineRef.Level->Tick(levelElapsed, false);
				Ticks = frameTime.Tick;
			}
			catch (const std::exception& e)
			{
				Fail(e.what());
			}
		}

		int Finish(const HeadlessRunSummary& summary) override
		{
			if (summary.TickLimitReached && ExitCode == 0)
				Fail("headless runner reached its tick limit before driver completion");

			const std::string status = ExitCode == 0 ? "complete" : "failed";
			const double simulatedSeconds = static_cast<double>(Ticks) * Config.GetFixedDelta();
			BotBenchmarkRunSummary runSummary(status, ExitCode, Ticks, simulatedSeconds,
				EngineRef.LaunchInfo.gameName, EngineRef.LaunchInfo.gameVersionString,
				EngineRef.LevelInfo ? EngineRef.LevelInfo->URL.Map : std::string(),
				BotClass, BotName, FailureReason);
			std::filesystem::create_directories(Config.GetOutputDirectory());
			const std::filesystem::path summaryPath = std::filesystem::path(Config.GetOutputDirectory()) / "summary.json";
			File::write_all_text(summaryPath.string(), runSummary.ToJson(Config));
			LogMessage("Bot benchmark summary: " + summaryPath.string());
			return ExitCode;
		}

	private:
		void SetupMapAndPlayer()
		{
			EngineRef.LaunchInfo.noEntryMap = true;
			EngineRef.LaunchInfo.url = Config.GetURL();
			UnrealURL url(EngineRef.GetDefaultURL(EngineRef.packages->GetIniValue("system", "URL", "LocalMap")), Config.GetURL());
			url.AddOrReplaceOption("Bots=0");
			url.AddOrReplaceOption("MinPlayers=0");
			url.AddOrReplaceOption("InitialBots=0");
			url.AddOrReplaceOption("OverrideClass=Botpack.CHSpectator");
			EngineRef.LoadMap(url);

			if (EngineRef.GameInfo)
			{
				if (EngineRef.GameInfo->HasProperty("RemainingBots"))
					EngineRef.GameInfo->SetInt("RemainingBots", 0);
				if (EngineRef.GameInfo->HasProperty("InitialBots"))
					EngineRef.GameInfo->SetInt("InitialBots", 0);
				if (EngineRef.GameInfo->HasProperty("MinPlayers"))
					EngineRef.GameInfo->SetInt("MinPlayers", 0);
			}
			EngineRef.LoginPlayer();

			UPlayerPawn* viewportPawn = EngineRef.viewport ? EngineRef.viewport->Actor() : nullptr;
			UPlayerReplicationInfo* pri = viewportPawn ? viewportPawn->PlayerReplicationInfo() : nullptr;
			if (!viewportPawn || !viewportPawn->IsA("Spectator") || !pri || !pri->bIsSpectator())
				Fail("benchmark viewport login did not produce a spectator");
		}

		void SetupBot()
		{
			if (!EngineRef.GameInfo || !EngineRef.GameInfo->HasProperty("BotConfig"))
			{
				Fail("GameInfo has no BotConfig; Botpack.DeathMatchPlus is required");
				return;
			}
			UObject* botConfig = EngineRef.GameInfo->GetUObject("BotConfig");
			if (!botConfig || !botConfig->HasProperty("Difficulty"))
			{
				Fail("BotConfig has no Difficulty property");
				return;
			}

			if (EngineRef.GameInfo->HasProperty("MinPlayers"))
				EngineRef.GameInfo->SetInt("MinPlayers", 0);
			if (EngineRef.GameInfo->HasProperty("InitialBots"))
				EngineRef.GameInfo->SetInt("InitialBots", 0);
			if (EngineRef.GameInfo->HasProperty("bRequireReady"))
				EngineRef.GameInfo->SetBool("bRequireReady", false);
			if (botConfig->HasProperty("bAdjustSkill"))
				botConfig->SetBool("bAdjustSkill", false);
			if (botConfig->HasProperty("MinPlayers"))
				botConfig->SetInt("MinPlayers", 0);
			if (botConfig->HasProperty("InitialBots"))
				botConfig->SetInt("InitialBots", 0);
			botConfig->SetInt("Difficulty", Config.GetDifficulty());

			std::set<UPawn*> existingBots;
			for (UActor* actor : EngineRef.Level->Actors)
			{
				UPawn* pawn = UObject::TryCast<UPawn>(actor);
				if (pawn && pawn->IsA("Bot"))
					existingBots.insert(pawn);
			}

			if (!EngineRef.ExecCommand({ "AddBots", "1" }))
			{
				Fail("AddBots exec function was not found");
				return;
			}
			std::vector<UPawn*> newBots;
			for (UActor* actor : EngineRef.Level->Actors)
			{
				UPawn* pawn = UObject::TryCast<UPawn>(actor);
				if (pawn && pawn->IsA("Bot") && existingBots.find(pawn) == existingBots.end())
					newBots.push_back(pawn);
			}
			if (newBots.size() != 1)
			{
				Fail("AddBots did not create exactly one controlled bot");
				return;
			}

			UPawn* bot = newBots.front();
			CallEvent(bot, "InitializeSkill", { ExpressionValue::FloatValue(static_cast<float>(Config.GetDifficulty())) });
			const bool expectedNovice = Config.GetDifficulty() < 4;
			const float expectedSkill = static_cast<float>(expectedNovice ? Config.GetDifficulty() : Config.GetDifficulty() - 4);
			if (!bot->HasProperty("bNovice") || bot->GetBool("bNovice") != expectedNovice ||
				std::abs(bot->Skill() - expectedSkill) >= 0.001f)
			{
				Fail("spawned bot skill did not match the requested external tier");
				return;
			}

			BotClass = UObject::GetUClassFullName(bot).ToString();
			if (UPlayerReplicationInfo* pri = bot->PlayerReplicationInfo())
				BotName = pri->PlayerName();
		}

		void Fail(std::string reason)
		{
			if (ExitCode == 0)
			{
				ExitCode = 2;
				FailureReason = std::move(reason);
				LogMessage("Bot benchmark failed: " + FailureReason);
			}
			Complete = true;
		}

		Engine& EngineRef;
		const BotBenchmarkRunConfig Config;
		uint64_t Ticks = 0;
		int ExitCode = 0;
		bool Complete = false;
		std::string FailureReason;
		std::string BotClass;
		std::string BotName;
	};

	BotBenchmarkRunConfig ConfigFromCommandLine()
	{
		return BotBenchmarkRunConfig::Parse(
			commandline ? commandline->GetArg("", "--botbench-url") : std::string(),
			commandline ? commandline->GetArg("", "--botbench-output") : std::string(),
			commandline ? commandline->GetArg("", "--botbench-seed") : std::string(),
			commandline ? commandline->GetArg("", "--botbench-ticks") : std::string(),
			commandline ? commandline->GetArg("", "--botbench-fixed-delta") : std::string(),
			commandline ? commandline->GetArg("", "--botbench-difficulty") : std::string());
	}
}

void RegisterBotBenchmarkDriver(HeadlessDriverRegistry& registry)
{
	if (!registry.Contains("bot-benchmark"))
	{
		registry.Register("bot-benchmark", [](Engine& engine)
		{
			return std::make_unique<BotBenchmarkDriver>(engine, ConfigFromCommandLine());
		});
	}
}
