//=============================================================================
// SuperTickProbe
//
// Mirrors ScriptedPawn's shape exactly: an inherited state Tick calls
// Global.Tick, the class Tick calls Super.Tick, and everything that matters
// runs after that Super call returns. If AfterSuperCalls stays at zero while
// the parent's GlobalCalls climbs, the Super call is not returning into its
// caller, which is what ScriptedPawn.Tick's frozen timers look like.
//=============================================================================
class SuperTickProbe extends GlobalTickProbe;

var int   AfterSuperCalls;
var float AfterSuperAccum;

function Tick( float DeltaTime )
{
	Super.Tick(DeltaTime);
	AfterSuperCalls++;
	AfterSuperAccum += DeltaTime;
}
