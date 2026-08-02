//=============================================================================
// Auto-attaches AllianceTelemetryMutator on every level, so testing doesn't
// require summon/console/URL tricks. Deus Ex runs one GameInfo class for the
// whole campaign, so pointing DefaultGame at this subclass (instead of
// DeusEx.DeusExGameInfo) gets our mutator spawned on every InitGame() call.
//
//   DefaultGame=AllianceTelemetry.AllianceTelemetryGameInfo
//
// Not chained in via AddMutator() -- the mutator only needs PostBeginPlay()/
// Tick() to run, so a plain Spawn() is enough; it doesn't need
// CheckReplacement/ModifyLogin hooks.
//
// Also auto-travels straight past the menu/intro. Level.ServerTravel() from
// InitGame() turned out to be a no-op in standalone singleplayer (it's meant
// for a listen/dedicated server commanding *connected clients* to travel --
// nothing to command when Level.NetMode is NM_Standalone). The mechanism that
// does work is the same one a player's own "open <map>?options" console
// command uses (proven earlier via a manual keybind test).
//
// Hooking this from DX (the main menu map) rather than Entry matters -- Entry
// has its own scripted splash-timer travel to DX.dx that fires shortly after
// InitGame() returns, so anything queued from Entry's InitGame() gets raced
// and overwritten. DX.dx never issues its own travel (it just waits for a
// menu click), so nothing can race against our request there.
//
// Firing ConsoleCommand() straight from PostLogin() hung the engine: PostLogin
// runs mid-bring-up, well before the viewport/D3D renderer exists (it lands
// before "Init: Input system initialized" and the D3DDrv bind in the log), so
// the travel/console machinery it depends on isn't there yet. Deferred a few
// seconds via Tick() instead -- by the time Tick() ever runs at all, the level
// is fully up and the client is rendering, so the same call is safe there.
//
// Only spawn the telemetry mutator on the actual target map, not on every
// level InitGame() fires for (Entry/DX included): StatLogFile's OpenLog()
// looks like it tracks the "alliancetelemetry" log as a single named slot --
// the first instance to open it (Entry's) claimed the real file handle, and
// every later instance's OpenLog()/Flush() silently no-op'd, so all telemetry
// after the very first level was being lost even though PostBeginPlay/Tick
// ran fine and logged no errors. Spawning exactly once, on the map we
// actually care about, sidesteps the collision entirely.
//=============================================================================
class AllianceTelemetryGameInfo extends DeusExGameInfo;

var string AutoTravelFromMap;
var string AutoTravelToMap;
var float  AutoTravelDelay;

var bool       bPendingAutoTravel;
var float      AutoTravelAccum;
var PlayerPawn AutoTravelPlayer;

event InitGame( string Options, out string Error )
{
	local AllianceTelemetryMutator M;
	local string CurrentMap;

	Super.InitGame( Options, Error );

	CurrentMap = string(Level.Outer.Name);
	log( "AllianceTelemetryGameInfo: InitGame reached, map="$CurrentMap$" AutoTravelToMap="$AutoTravelToMap$" Options="$Options );

	// Level.Outer.Name isn't populated yet at this point under Surreal Engine
	// (reads "None"), unlike retail DeusEx.exe where it's already live here --
	// in that case trust the URL-forced ?game=AllianceTelemetryGameInfo launch
	// itself as proof we're on the intended map rather than gating on a name
	// we can't read.
	if ( CurrentMap != "None" && CurrentMap != AutoTravelToMap )
		return;

	M = Spawn( class'AllianceTelemetryMutator' );
	if ( M == None )
		log( "AllianceTelemetryGameInfo: Spawn(AllianceTelemetryMutator) FAILED" );
	else
		log( "AllianceTelemetryGameInfo: spawned "$string(M.Name) );
}

event PostLogin( PlayerPawn NewPlayer )
{
	Super.PostLogin( NewPlayer );

	// No "None" fallback here (unlike InitGame above): if Level.Outer.Name is
	// unreadable this early under Surreal Engine, we can't tell DX apart from
	// the already-reached target map, and guessing wrong would re-trigger a
	// travel back onto the map we're already standing on.
	if ( AutoTravelToMap != "" && string(Level.Outer.Name) == AutoTravelFromMap )
	{
		bPendingAutoTravel = True;
		AutoTravelAccum = 0;
		AutoTravelPlayer = NewPlayer;
	}
}

event Tick( float DeltaTime )
{
	local string Dummy;

	if ( !bPendingAutoTravel )
		return;

	AutoTravelAccum += DeltaTime;
	if ( AutoTravelAccum < AutoTravelDelay )
		return;

	bPendingAutoTravel = False;
	if ( AutoTravelPlayer == None )
		return;

	log( "AllianceTelemetryGameInfo: auto-travelling from "$AutoTravelFromMap$" to "$AutoTravelToMap );
	Dummy = AutoTravelPlayer.ConsoleCommand( "open "$AutoTravelToMap$"?Name=Player?Class=DeusEx.JCDentonMale" );
}

defaultproperties
{
	AutoTravelFromMap="DX"
	AutoTravelToMap="01_NYC_UNATCOIsland"
	AutoTravelDelay=3.000000
	bAlwaysTick=True
}
