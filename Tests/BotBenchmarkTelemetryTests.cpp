#include "BotBenchmark/BotBenchmarkProtocol.h"
#include "BotBenchmark/BotBenchmarkTelemetry.h"

#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace
{
	int Fail(const char* message)
	{
		std::cerr << message << '\n';
		return 1;
	}
}

int main()
{
	const BotBenchmarkRunConfig config = BotBenchmarkRunConfig::Parse(
		"DM-Test?Game=Botpack.DeathMatchPlus", "evidence", "18446744073709551615", "72", "0.02", "7");
	const std::string configId = BotBenchmarkTelemetryProtocol::ConfigIdentity(config);
	if (configId != "fnv1a64:03da62fa895e0e9f")
		return Fail("bot benchmark configuration identity changed");
	if (BotBenchmarkTelemetryProtocol::EventCap(config.GetMaxTicks()) != 74)
		return Fail("bot benchmark telemetry cap was not tied to max ticks");

	const std::string expectedManifest =
		"{\n"
		"  \"schema\": \"surreal-bot-benchmark-manifest-v1\",\n"
		"  \"driver\": \"bot-benchmark\",\n"
		"  \"config_id\": \"fnv1a64:03da62fa895e0e9f\",\n"
		"  \"url\": \"DM-Test?Game=Botpack.DeathMatchPlus\",\n"
		"  \"output_directory\": \"evidence\",\n"
		"  \"seed\": \"18446744073709551615\",\n"
		"  \"max_ticks\": \"72\",\n"
		"  \"fixed_delta\": 0.020000000,\n"
		"  \"difficulty\": 7,\n"
		"  \"telemetry_event_cap\": \"74\"\n"
		"}\n";
	if (BotBenchmarkTelemetryProtocol::ManifestJson(config) != expectedManifest)
		return Fail("bot benchmark manifest serialization was not exact");

	BotBenchmarkBotState second;
	second.Identity = "pri:2";
	second.Actor = "Bot2";
	second.PlayerName = "B\"ot";
	second.ClassName = "Botpack.Bot";
	second.State = "Roaming";
	second.PositionX = 1.25;
	second.PositionY = -2.5;
	second.PositionZ = -0.0;
	second.VelocityZ = 3.0;
	second.Health = 87;

	BotBenchmarkBotState first;
	first.Identity = "pri:1";
	first.Actor = "Bot1";
	first.PlayerName = "Line\nBreak";
	first.ClassName = "Botpack.Bot";
	first.State = "Attacking";
	first.Health = 100;

	BotBenchmarkTelemetryEvent event;
	event.Sequence = 5;
	event.Tick = 4;
	event.SimulatedSeconds = 0.08;
	event.Type = "tick";
	event.Map = "DM-\"Test";
	event.Status = "running";
	event.Bots = { second, first };
	const std::string expectedEvent =
		"{\"schema\":\"surreal-bot-benchmark-telemetry-v1\",\"seq\":\"5\",\"config_id\":\"fnv1a64:03da62fa895e0e9f\",\"tick\":\"4\",\"simulated_seconds\":0.080000000,\"type\":\"tick\",\"map\":\"DM-\\\"Test\",\"status\":\"running\",\"failure_reason\":\"\",\"bots\":["
		"{\"identity\":\"pri:1\",\"actor\":\"Bot1\",\"player_name\":\"Line\\nBreak\",\"class\":\"Botpack.Bot\",\"position\":{\"x\":0.000000,\"y\":0.000000,\"z\":0.000000},\"velocity\":{\"x\":0.000000,\"y\":0.000000,\"z\":0.000000},\"health\":100,\"state\":\"Attacking\"},"
		"{\"identity\":\"pri:2\",\"actor\":\"Bot2\",\"player_name\":\"B\\\"ot\",\"class\":\"Botpack.Bot\",\"position\":{\"x\":1.250000,\"y\":-2.500000,\"z\":0.000000},\"velocity\":{\"x\":0.000000,\"y\":0.000000,\"z\":3.000000},\"health\":87,\"state\":\"Roaming\"}]}\n";
	if (BotBenchmarkTelemetryProtocol::EventJson(configId, event) != expectedEvent)
		return Fail("bot benchmark event ordering, formatting, or escaping changed");

	bool rejectedNonFinite = false;
	try
	{
		event.Bots.front().VelocityX = std::numeric_limits<double>::infinity();
		BotBenchmarkTelemetryProtocol::EventJson(configId, event);
	}
	catch (const std::invalid_argument&)
	{
		rejectedNonFinite = true;
	}
	if (!rejectedNonFinite)
		return Fail("bot benchmark telemetry accepted a non-finite number");

	return 0;
}
