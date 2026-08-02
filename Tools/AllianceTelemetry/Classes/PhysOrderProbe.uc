//=============================================================================
// PhysOrderProbe
//
// Deus Ex pawns land at one collision height in retail and a different one in
// SurrealEngine because ScriptedPawn shrinks its cylinder by 4.5 in the frame
// it first falls, so the landing height depends on whether the engine runs an
// actor's physics before or after that actor's script.
//
// The probe drifts at a constant velocity and compares the distance physics
// actually moved it against the DeltaTime the engine has handed to its Tick.
// Physics running after the script leaves the distance one frame behind the
// accumulated time; physics running first leaves them equal. It is teleported
// back every frame so it cannot drift out of the level and stop ticking.
//=============================================================================
class PhysOrderProbe extends Actor;

var vector BaseLoc;
var float  DriftSpeed;
var float  SumDT;         // DeltaTime the engine has handed this actor
var float  Traveled;      // distance physics has actually moved it
var float  Lag;           // SumDT - Traveled/DriftSpeed, in seconds
var float  LagMin, LagMax;
var int    Frames;
var int    MovedBetween;  // frames where physics ran between the Tick event and the state code
var float  TickT, CodeT, TickZ;

function PostBeginPlay()
{
	Super.PostBeginPlay();
	BaseLoc = Location;
	Velocity = vect(0,0,-1) * DriftSpeed;
	SetPhysics(PHYS_Projectile);
	LagMin = 1000;
	LagMax = -1000;
}

auto state Probing
{
	function Tick( float DeltaTime )
	{
		Traveled += BaseLoc.Z - Location.Z;
		SetLocation(BaseLoc);
		Velocity = vect(0,0,-1) * DriftSpeed;
		SumDT += DeltaTime;
		Frames++;
		Lag = SumDT - Traveled / DriftSpeed;
		if ( Frames > 2 )
		{
			if ( Lag < LagMin ) LagMin = Lag;
			if ( Lag > LagMax ) LagMax = Lag;
		}
		TickT = Level.TimeSeconds;
		TickZ = Location.Z;
	}

Begin:
	CodeT = Level.TimeSeconds;
	if ( (CodeT == TickT) && (Location.Z != TickZ) )
		MovedBetween++;
	Sleep(0.0);
	Goto('Begin');
}

defaultproperties
{
	DriftSpeed=200.000000
	bHidden=True
	bStatic=False
	bNoDelete=False
	bCollideWorld=False
	bCollideActors=False
	bBlockActors=False
	bBlockPlayers=False
	RemoteRole=ROLE_None
}
