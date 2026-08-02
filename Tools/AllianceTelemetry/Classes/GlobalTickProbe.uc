//=============================================================================
// GlobalTickProbe
//
// Deus Ex's ScriptedPawn reaches its class-level Tick only through
// Global.Tick(deltaSeconds) from each state's Tick, and every accumulator in
// that body is frozen under SurrealEngine while the same body's non-timing
// work still runs. This isolates that dispatch from Deus Ex entirely: one
// state Tick, one global Tick, and separate counters for "was it called" and
// "what argument did it receive".
//=============================================================================
class GlobalTickProbe extends Actor;

var int   GlobalCalls;
var int   StateCalls;
var float GlobalAccum;
var float StateAccum;
var float LastGlobalDelta;
var float LastStateDelta;

function Tick( float DeltaTime )
{
	GlobalCalls++;
	GlobalAccum += DeltaTime;
	LastGlobalDelta = DeltaTime;
}

auto state Probing
{
	function Tick( float DeltaTime )
	{
		StateCalls++;
		StateAccum += DeltaTime;
		LastStateDelta = DeltaTime;
		Global.Tick(DeltaTime);
	}
}

defaultproperties
{
	bHidden=True
	bStatic=False
	bNoDelete=False
	RemoteRole=ROLE_None
}
