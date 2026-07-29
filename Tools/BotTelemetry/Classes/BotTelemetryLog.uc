//=============================================================================
// Line logger for bot telemetry.
//
// StatLogFile.LogEventString() flushes on every single line, which is far too
// slow at the sample rates needed to reconstruct trajectories. This writes
// through FileLog() and flushes once per sample instead.
//=============================================================================
class BotTelemetryLog extends StatLogFile;

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
	OutputName="bottelemetry"
	bWorld=False
	bWatermark=False
	LocalLogDir="../Logs"
}
