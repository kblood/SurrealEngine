//=============================================================================
// TickOrderProbe
//
// Deus Ex's ScriptedPawn.StartUp runs InitializePawn() from both its state code
// and its state Tick, and the Tick copy calls FollowOrders() straight after. A
// pawn the map marks bInWorld=False therefore ends up Idle or Patrolling purely
// according to which of the two the engine runs first in a frame. Both stamp
// Level.TimeSeconds; the state code compares the two stamps to see whether the
// Tick for this same frame has already happened.
//=============================================================================
class TickOrderProbe extends Actor;

var float TickStamp;
var int   TickFirst;
var int   CodeFirst;
var int   Frames;

auto state Probing
{
	function Tick( float DeltaTime )
	{
		TickStamp = Level.TimeSeconds;
	}

Begin:
	if ( TickStamp == Level.TimeSeconds )
		TickFirst++;
	else
		CodeFirst++;
	Frames++;
	Sleep(0.0);
	Goto('Begin');
}

defaultproperties
{
	bHidden=True
	bStatic=False
	bNoDelete=False
	RemoteRole=ROLE_None
}
