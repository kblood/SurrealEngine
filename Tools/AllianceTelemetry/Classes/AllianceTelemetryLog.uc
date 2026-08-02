//=============================================================================
// Line logger for alliance telemetry. Same StatLogFile-through-FileLog()
// pattern as BotTelemetryLog: flush once per batch instead of once per line.
//=============================================================================
class AllianceTelemetryLog extends StatLogFile;

var string OutputName;

function StartLog()
{
	StatLogFile  = LocalLogDir$"/"$OutputName$".tmp";
	StatLogFinal = LocalLogDir$"/"$OutputName$".log";
	OpenLog();
}

function StopLog()
{
	FileFlush();
	CloseLog();
}

// StatLog schedules a ping-logging timer that would pollute the stream.
function BeginPlay() {}
function Timer() {}

final function Line( string S )
{
	FileLog(S);
}

final function Flush()
{
	FileFlush();
}

defaultproperties
{
	OutputName="alliancetelemetry"
	bWorld=False
	bWatermark=False
	LocalLogDir="../Logs"
}
