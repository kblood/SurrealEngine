//=============================================================================
// Deus Ex only. Emits ScriptedPawn alliance setup and the live alliance table,
// to observe what ScriptedPawn.InitializeAlliances() actually configures per
// map and how the runtime table (AlliancesEx) evolves during play.
//
//   ?Mutator=AllianceTelemetry.AllianceTelemetryMutator
//
// Deliberately has no compile-time dependency on the DeusEx package (its
// class source isn't available to us) -- ScriptedPawn instances are found via
// IsA('ScriptedPawn') and their fields are read via GetPropertyText(), both
// inherited from Object, so EditPackages only needs Core/Engine/Editor.
//
// Output lands in ../Logs/alliancetelemetry.tmp (tab-separated; flushed after
// every batch, so it's safe to read while the game is still running):
//   #  header / status lines
//   I  one row per ScriptedPawn, dumped once after InitialDumpDelay seconds
//      (by which point every placed pawn's Begin: state, and therefore
//      InitializePawn()/InitializeAlliances(), has already run) -- the
//      InitialAlliances field is the raw exported struct-array text
//      (AllianceName/AllianceLevel/bPermanent per populated slot)
//   E  live snapshot per living ScriptedPawn, sampled every 1/SampleHz
//      seconds -- AlliancesEx is the raw exported struct-array text
//      (AllianceName/AllianceLevel/AgitationLevel/bPermanent per slot)
//=============================================================================
class AllianceTelemetryMutator extends Mutator config(AllianceTelemetry);

// Pawns come to rest at different heights in the two engines. The settling
// happens in the first fraction of a second, long before the regular samplers
// start, so this dumps every pawn's height and cylinder once per frame from the
// very first tick until it expires.
var config int ScoutPickTrials;
var config float ScoutPickMaxDist;

// Population probe: parks the player inside one generator's ActiveArea so it
// actually generates, then counts what it produces. Without this the map's
// generators never fire in either engine -- the player's route never comes
// within ActiveArea of any of them.
var config string PopGenerator;
var config float  PopOffset;
var config float  PopCensusPeriod;
var config bool   bPopFaceGenerator;
// Sends a LoudNoise of a known volume and radius from a known point, so the two
// engines can be compared on delivery alone rather than on which actor happened
// to make a noise. Zero period leaves the probe off.
var config float  NoiseProbePeriod;
var config float  NoiseProbeRadius;
var config float  NoiseProbeVolume;
var config float  NoiseProbeDist;
var config bool   bNoiseProbeFromPlayer;
// A bird that has already fled is deaf to the next probe, which starves the
// sweep of samples. Putting them back in Wandering first makes every probe
// count, at the cost of no longer measuring how long Flying lasts.
var config bool   bNoiseProbeReset;
var bool          bNoiseResetDone;
var float         NoiseAccum;
var float         PopAccum;
var bool          bPopPlaced;
var PawnGenerator PopGen;
var vector        PopPlayerLoc;
var rotator       PopPlayerRot;

var config float EarlyBurstSeconds;
var float EarlyBurstLeft;
var int   EarlyBurstFrame;

var config float InitialDumpDelay; // seconds to wait before dumping InitialAlliances
var config float SampleHz;         // rate for the live AlliancesEx snapshot
var config float AutoQuitDelay;    // seconds after the initial dump before auto-quitting; 0 disables

// Perception A/B testing: puts the player next to a named ScriptedPawn (Paul
// Denton on the docks by default) without ever frobbing it, so live State/
// Enemy telemetry shows whether that pawn notices and approaches on its own --
// the same GOTY-vs-Surreal comparison this mutator exists to run under both
// engines against the identical DeusEx.u script.
var config bool   bTeleportPlayerToTarget;
var config string TeleportTargetName;
var config vector PaulApproachOffset;

// The target's own state machine (MissionScript.InitStateMachine) isn't set up
// yet on the very first tick or two -- GetStateName() can still read the
// pre-init default there rather than 'Conversation', letting the teleport race
// past the guard below before the scripted intro has actually started. Don't
// even attempt a teleport before this much time has passed.
var config float MinTeleportDelay;

// Re-places the player in front of the target every tick, so the pawn gets a
// sustained look at it instead of a two-second glimpse before it patrols away.
var config bool  bKeepPlayerAtTarget;
var config float ApproachDistance;

// Deus Ex ScriptedPawn AI state, read by name so this package keeps no compile
// -time dependency on DeusEx.u. Dumped for the teleport target in both engines
// and diffed: the UnrealScript driving the AI is byte-identical between them,
// so any field that differs is engine-caused. These are the fields on the
// CheckEnemyPresence -> CheckCycle -> SetEnemy path.
var string ProbeProps[32];
// Drops every sampler but SampleRanges, so a run measures engine tick rate
// without the per-sample GetPropertyText and string-concatenation cost.
var config bool bMinimalSampling;
// Burns work every frame to drive the engine's tick rate down on demand. Used
// to test whether ScriptedPawn shot cadence is bound by tick period: if it is,
// the interval between shots scales linearly with 1/tickrate.
var config int TickLoadIters;
var float LoadSink;

// 5 Hz polling is too coarse to time the latent waits inside ContinueFire, so
// these track the previous frame's values and emit a row only when one changes.
var config bool bLogTransitions;
var name  PrevAnim;
var int   PrevAmmo;
var bool  PrevReady;
var string PrevWeaponState;
var int PrevClip;
var int TickN;
var int LastTickN;
var float LastTickTime;
var vector LastTargetLoc;

// GetShotTime() returns ShotTime*(BaseAccuracy*2+1) for a ScriptedPawn owner, so
// overriding BaseAccuracy changes the weapon's cooldown by a known factor. Both
// engines read the same value, so a re-arm interval that moves in one engine and
// not the other says which one is actually honouring the script's Sleep.
// Empty leaves the pawn's own value alone. Re-applied every frame in case the
// pawn's AI writes it back.
var config string ForceBaseAccuracy;
var bool bAccuracyForced;

// Retail's weapon re-arms in ~0.6x the Sleep(GetShotTime()) its own script asks
// for. This measures a plain latent Sleep against Level.TimeSeconds to tell a
// weapon-specific effect from a general one in how Sleep is accounted.
var config bool bSleepTest;
var float SleepT0;

// Names a specific pair of pawns to probe every sample, so the perception inputs
// behind one engine acquiring an enemy the other never does can be compared
// directly instead of inferred from the resulting enemy lists.
var config string ProbePawnA;
var config string ProbePawnB;

var config bool bTickDump;
var int TickDumpN;

var int PrevAlive;
var int PrevArmed;
var int PrevFighting;

var AllianceTelemetryLog Sink;
var GlobalTickProbe TickProbe;
var GlobalTickProbeChild TickProbeChild;
var SuperTickProbe SuperProbe;
var TickOrderProbe OrderProbe;
var PhysOrderProbe PhysProbe;
var bool  bDumpedInitial;
var config bool bSeatTrace;   // seat diagnostics: perturbs pawn path state, off for regular captures
var bool  bTickLogged;
var bool  bQuitRequested;
var bool  bTeleportDone;
var float Accum;
var float Period;
var float QuitAccum;
var float ElapsedTime;
var int   Seq;

function PostBeginPlay()
{
	Super.PostBeginPlay();

	// Actors spawned this early (during GameInfo.InitGame(), i.e. mid level
	// bring-up) get PostBeginPlay fired twice: once immediately as part of
	// Spawn(), and again during the level's own "bring placed/spawned actors
	// to life" pass. A second Sink.StartLog() collided with the first's still
	// -open handle and silently ate every write after it -- guard re-entry.
	if ( Sink != None )
	{
		log( "AllianceTelemetryMutator: PostBeginPlay re-entered on "$string(Name)$", ignoring" );
		return;
	}

	log( "AllianceTelemetryMutator: PostBeginPlay on "$string(Name) );

	if ( SampleHz <= 0 )
		SampleHz = 1;
	Period = 1.0 / SampleHz;

	Sink = Spawn(class'AllianceTelemetryLog');
	if ( Sink == None )
	{
		log( "AllianceTelemetryMutator: Spawn(AllianceTelemetryLog) FAILED" );
		return;
	}
	Sink.StartLog();

	TickProbe = Spawn(class'GlobalTickProbe');
	TickProbeChild = Spawn(class'GlobalTickProbeChild');
	SuperProbe = Spawn(class'SuperTickProbe');
	OrderProbe = Spawn(class'TickOrderProbe');
	PhysProbe = Spawn(class'PhysOrderProbe');

	Emit("#"$Chr(9)$"alliancetelemetry"$Chr(9)$"v1"
		$Chr(9)$"map="$string(Level.Outer.Name)
		$Chr(9)$"title="$Level.Title
		$Chr(9)$"game="$string(Level.Game.Class));
	Emit("#"$Chr(9)$"cols_I"$Chr(9)$"name class alliance bInitialized InitialAlliances");
	Emit("#"$Chr(9)$"cols_O"$Chr(9)$"name orders orderTag orderActor seatActor bSitAnywhere initialState state");
	Emit("#"$Chr(9)$"cols_M"$Chr(9)$"time name state seatActor seatSlot bSeatLocationValid bSitting bUseFirstSeatOnly bSeatHackUsed seatHack orderActor");
	Emit("#"$Chr(9)$"cols_H"$Chr(9)$"name class location initialPosition numSitPoints sittingActor bDeleteMe bWaterZone zoneNumber");
	Emit("#"$Chr(9)$"cols_U"$Chr(9)$"pawn seat dist actorReachable findPathToward isSeatValid moved numSitPoints sitting0 water");
	Emit("#"$Chr(9)$"cols_E"$Chr(9)$"seq ms name state enemy AlliancesEx");
	Emit("#"$Chr(9)$"cols_D"$Chr(9)$"seq ms target name=value;...");
	// GetPropertyText only reports element 0 of a static array, so the live
	// AlliancesEx slots have to be indexed one at a time.
	Emit("#"$Chr(9)$"cols_A"$Chr(9)$"seq ms name enemy agitationTimer bAlliancesChanged decayRate sustain pawnAllianceType isValidEnemy state agitationCheckTimer weaponTimer alarmTimer reloadTimer fireTimer");
	Emit("#"$Chr(9)$"cols_X"$Chr(9)$"seq ms name distanceFromPlayer lastRendered bTickVisibleOnly bCheckOther bHidden enemy lightHere x y z enemyReadiness reactionLevel seekLevel sightPercentage bSeekLocation seekType");
	Emit("#"$Chr(9)$"cols_DR"$Chr(9)$"seq ms augBallistic augShield augEnviro augTarget skillEnviro combatDifficulty ballisticArmor health healthTorso healthHead armLeft armRight legLeft legRight reduce25 netMode");
	Emit("#"$Chr(9)$"cols_PT"$Chr(9)$"seq ms pawn state orders orderTag destPoint moveTarget x y z speed walkSpeed groundSpeed physics bStasis anim randomWandering restlessness");
	Emit("#"$Chr(9)$"cols_PT_ext"$Chr(9)$"bInWorld bHidden lastRendered bInitialized collRadius collHeight prePivotZ drawScale bCollideWorld floorDist defCollHeight prePivotOffsetZ");
	Emit("#"$Chr(9)$"cols_TO"$Chr(9)$"seq ms frames tickFirst codeFirst");
	Emit("#"$Chr(9)$"cols_PO"$Chr(9)$"seq ms frames movedBetween traveled sumDT driftSpeed lag lagMin lagMax physics");
	Emit("#"$Chr(9)$"cols_EB"$Chr(9)$"seq ms frame dt timeSeconds pawn z velZ collHeight defCollHeight physics state bInWorld prePivotOffsetZ base");
	Emit("#"$Chr(9)$"cols_PG"$Chr(9)$"seq ms gen class pawnCount maxCount poolCount frequency radius bDying scout bRandomTypes bRepopulate pawnClasses state");
	Emit("#"$Chr(9)$"cols_AL"$Chr(9)$"name class x y z bHidden bStatic bNoDelete bCollideActors physics");
	Emit("#"$Chr(9)$"cols_SC"$Chr(9)$"seq ms gen class trial dist ok canSee unnec dz dxy distFromPlayer trueDist physics");
	Emit("#"$Chr(9)$"cols_PN"$Chr(9)$"seq ms timeSeconds pigeons seagulls rats flies fish playerDist playerSpeed playerPhysics playerIsWalking");
	Emit("#"$Chr(9)$"cols_NZ"$Chr(9)$"seq ms timeSeconds pawn class distFromGen speed mass physics state");
	Emit("#"$Chr(9)$"cols_NP"$Chr(9)$"seq ms timeSeconds pawn distFromSource radius volume stateBefore source los losRaised");
	Emit("#"$Chr(9)$"cols_PC"$Chr(9)$"seq ms gen pawnCount poolCount maxCount bDying distFromPlayer activeArea");
	Emit("#"$Chr(9)$"cols_PB"$Chr(9)$"seq ms timeSeconds pawn distFromPlayer trueDist lastRendered distFromGen z state physics health bHidden bUseHome homeExtent distFromHome traceHome reachHome bDefendHome wanderlust bStasis bTickVisibleOnly velX velY velZ accX accY"$" pawnZoneNum playerZoneNum pawnZone playerZone fleeBig blockActors blockPlayers startler startlerSpeed");
	Emit("#"$Chr(9)$"cols_TA"$Chr(9)$"seq ms pawn idx actor class isLevel dist hitX hitY hitZ nx ny nz traceLen aX aY aZ aRadius aHeight aCollide aBlock sX sY sZ eX eY eZ");
	Emit("#"$Chr(9)$"cols_AW"$Chr(9)$"seq ms pawn weapon hitDamage clipCount accurateRange maxRange distToPlayer enemy yaw viewYaw yawToPlayer pitch viewPitch pitchToPlayer desiredYaw bRotateToDesired rotRateYaw physics focus moveTarget");
	Emit("#"$Chr(9)$"cols_J"$Chr(9)$"seq ms a b dist aiCanSee visibility lightAtB isValidEnemy allianceType stateA visThreshold minAngularSize angularResolution bRadius bHeight lightAtA");
	Emit("#"$Chr(9)$"cols_B"$Chr(9)$"seq ms stateCalls globalCalls stateAccum globalAccum lastStateDelta lastGlobalDelta timeSeconds");
	Emit("#"$Chr(9)$"cols_Y"$Chr(9)$"same as B, for a subclass that declares no Tick of its own");
	Emit("#"$Chr(9)$"cols_Z"$Chr(9)$"seq ms stateCalls globalCalls afterSuperCalls globalAccum afterSuperAccum timeSeconds");
	Emit("#"$Chr(9)$"cols_G"$Chr(9)$"seq ms target isValidEnemy isValidEnemyNoAlliance computedVis aiCanSee6"
		$" distanceFromPlayer lastRendered bTickVisibleOnly enemyReadiness cycleCumulative cycleCandidate"
		$" playerAlliance pawnAllianceType allianceTypeOfPlayerAlliance allianceTypeLiteral bReverseAlliances bLikesNeutral"
		$" playerHealth targetHealth enemyLastSeen enemyTimeout enemy");
	Emit("#"$Chr(9)$"cols_W"$Chr(9)$"seq ms target weapon bReadyToFire reloadTimer fireTimer aiFireDelay"
		$" ammo reloadCount clipCount aiCanShoot aiCanShootReady bCanFire bFacingTarget bReadyToReload"
		$" minRange maxRange animSequence animFrame animRate bFire"
		$" bAutomatic bFiring shotTime weaponState bInstantHit hitDamage accurateRange baseAccuracy calcAccuracy weaponSkill ownerDist");
	Emit("#"$Chr(9)$"cols_R"$Chr(9)$"seq ms target playerWeapon selfMin selfAccurate selfMax"
		$" enemyMin enemyAccurate enemyMax targetRadius playerRadius groundSpeed"
		$" frand1 frand2 frand3 frand4 tickDelta timeDelta velocity moved physics");
	Emit("#"$Chr(9)$"cols_T"$Chr(9)$"seq ms target anim animRate ammo bReadyToFire weaponState pawnState ownerIsSP clipCount reloadCount");
	Emit("#"$Chr(9)$"cols_S"$Chr(9)$"seq ms target lastSeenPos lastSeeingPos enemyLastSeen enemy pawnState bSeekLocation lastSeenDist elapsed tickN realClock");
	Emit("#"$Chr(9)$"cols_C"$Chr(9)$"seq ms aliveScriptedPawns armed fighting armedButOutOfAmmo");
	Emit("#"$Chr(9)$"cols_P"$Chr(9)$"seq ms target playerAIVisibility aiCanSee aiCanSeeVis1 dist targetState canSee lineOfSight bIsPlayer sightRadius peripheralVision aiCanSeeNoLight ambientBrightness playerRadius playerHeight");
	Sink.Flush();

	log( "AllianceTelemetryMutator: header written and flushed" );
}

function Tick( float DeltaTime )
{
	local int LoadIdx;

	if ( !bTickLogged )
	{
		bTickLogged = True;
		log( "AllianceTelemetryMutator: first Tick, dt="$DeltaTime );
	}

	ElapsedTime += DeltaTime;

	if ( EarlyBurstSeconds > 0 && EarlyBurstLeft < EarlyBurstSeconds )
	{
		EarlyBurstLeft += DeltaTime;
		SampleEarlyBurst(DeltaTime);
	}

	if ( bSeatTrace && ElapsedTime < 6.0 )
		SampleSeatTrace();

	if ( PopGenerator != "" )
		TickPopulationProbe(DeltaTime);

	// Kept outside the !bDumpedInitial gate below: AttemptTeleportPlayerToTarget()
	// now waits for the target to leave a scripted Conversation state (e.g. the
	// docks map's opening Paul/JC briefing), which can easily outlast
	// InitialDumpDelay -- it needs to keep retrying every tick for the whole run,
	// not just during the first second and a half.
	if ( bTeleportPlayerToTarget && ElapsedTime >= MinTeleportDelay
		&& ( !bTeleportDone || bKeepPlayerAtTarget ) )
		AttemptTeleportPlayerToTarget();

	if ( !bDumpedInitial )
	{
		InitialDumpDelay -= DeltaTime;
		if ( InitialDumpDelay <= 0 )
		{
			DumpInitialAlliances();
			DumpInitialOrders();
			DumpSeats();
			DumpActorList();
			DumpScoutPick();
			if ( bSeatTrace )
				DumpSeatReach();
			bDumpedInitial = True;
			if ( bSleepTest )
				GotoState('SleepTester');
			// One value per property type, to find where the two engines'
			// GetPropertyText formatting diverges.
			Emit("#"$Chr(9)$"typeprop"
				$Chr(9)$"str["$GetPropertyText("TeleportTargetName")$"]"
				$Chr(9)$"strArr["$GetPropertyText("ProbeProps")$"]"
				$Chr(9)$"name["$GetPropertyText("PrevAnim")$"]"
				$Chr(9)$"float["$GetPropertyText("SampleHz")$"]"
				$Chr(9)$"int["$GetPropertyText("TickLoadIters")$"]"
				$Chr(9)$"bool["$GetPropertyText("bTeleportPlayerToTarget")$"]"
				$Chr(9)$"vec["$GetPropertyText("PaulApproachOffset")$"]"
				$Chr(9)$"obj["$GetPropertyText("Sink")$"]");
			SampleLongExpr();
			Sink.Flush();
		}
		return;
	}

	TickN++;

	// One row per Tick call, against the real-world clock. If an engine calls Tick
	// more than once per frame with the same DeltaTime, the deltas arrive in
	// duplicated runs that share a millisecond stamp.
	if ( bTickDump && TickDumpN < 800 )
	{
		TickDumpN++;
		Emit("K"$Chr(9)$TickN$Chr(9)$DeltaTime
			$Chr(9)$(Level.Minute*60 + Level.Second + Level.Millisecond*0.001)
			$Chr(9)$Level.TimeSeconds);
		if ( TickDumpN == 800 )
			Sink.Flush();
	}

	for ( LoadIdx = 0; LoadIdx < TickLoadIters; LoadIdx++ )
		LoadSink += Sqrt( LoadIdx + 1 );

	// Ahead of the samplers on purpose: a runtime error inside one of them aborts
	// the rest of this Tick, and with the quit check last that left the run alive
	// forever with its log still unflushed, hiding the error that caused it.
	if ( AutoQuitDelay > 0 && !bQuitRequested )
	{
		QuitAccum += DeltaTime;
		if ( QuitAccum >= AutoQuitDelay )
		{
			bQuitRequested = True;
			log( "AllianceTelemetryMutator: auto-quit delay elapsed, requesting exit" );
			RequestQuit();
		}
	}

	if ( bLogTransitions )
		SampleTransitions();

	Accum += DeltaTime;
	if ( Accum >= Period )
	{
		Accum = 0;
		if ( !bMinimalSampling )
		{
			SampleLive();
			SampleSeek();
			SamplePerception();
			SampleProbeProps();
			SampleEnemyGate();
			SampleWeapon();
			SampleWeaponCensus();
			SampleAgitation();
			SampleRenderGate();
			SampleProbePair();
			SampleGlobalTick();
		}
		SampleRanges();
	}
}

// Finds the local player and issues the same "Exit" console command Quit-to-
// desktop uses, so the process shuts down cleanly (flushing DeusEx.log and
// this mutator's own log) instead of needing an external force-kill, which
// trips Deus Ex's own crash-recovery wizard on the next launch.
function RequestQuit()
{
	local Pawn P;
	local string Dummy;

	for ( P = Level.PawnList; P != None; P = P.NextPawn )
	{
		if ( P.IsA('PlayerPawn') )
		{
			Dummy = P.ConsoleCommand( "Exit" );
			return;
		}
	}
	log( "AllianceTelemetryMutator: RequestQuit found no PlayerPawn" );
}

// Retried every Tick (from PostBeginPlay onward) until both a PlayerPawn and
// the named target are found on the pawn list -- the mutator is spawned
// mid-InitGame(), before the player has logged in, so neither may exist yet
// on the very first few ticks. Silently gives up once the initial dump fires
// if the target/player never appear (e.g. a map without the named pawn).
function AttemptTeleportPlayerToTarget()
{
	local Pawn P, Target, Player;

	for ( P = Level.PawnList; P != None; P = P.NextPawn )
	{
		if ( P.IsA('PlayerPawn') )
			Player = P;
		else if ( string(P.Name) == TeleportTargetName )
			Target = P;
	}

	if ( Player == None || Target == None )
		return;

	// Skip while the target is mid scripted-cutscene dialogue (e.g. the docks
	// map's opening Paul/JC briefing): its own state machine is occupied
	// regardless of player proximity there, so teleporting in during that
	// window wouldn't test autonomous-approach behavior at all. Keep retrying
	// every tick until it clears.
	if ( Target.GetStateName() == 'Conversation' )
		return;

	// Placed along the target's facing vector rather than a fixed world-space
	// offset: a one-shot teleport to +X left the player behind the pawn and out
	// of view again within about two seconds of patrolling, which is far too
	// short an exposure to conclude anything from.
	Player.SetLocation( Target.Location + (Vector(Target.Rotation) * ApproachDistance) );

	if ( !bTeleportDone )
	{
		bTeleportDone = True;
		log( "AllianceTelemetryMutator: placed player in front of "$TeleportTargetName$", without frobbing -- watching for autonomous approach" );
	}
}

// Repeating: the raw perception inputs behind autonomous NPC noticing, sampled
// for the teleport target specifically. AICanSee() short-circuits to 0 whenever
// the visibility it is handed is <= 0, so logging AIVisibility() alongside it
// separates "the pawn genuinely cannot see the player" from "the visibility
// term fed into the check is zero before any geometry is considered".
function SamplePerception()
{
	local Pawn P, Target, Player;
	local Actor A;
	local int nVisible;
	local int nVisiblePawns;
	local bool bPlayerVisible;

	for ( P = Level.PawnList; P != None; P = P.NextPawn )
	{
		if ( P.IsA('PlayerPawn') )
			Player = P;
		else if ( string(P.Name) == TeleportTargetName )
			Target = P;
	}

	if ( Player == None || Target == None )
		return;

	// Deus Ex's own enemy scan walks these iterators rather than Level.PawnList,
	// so if they come back empty the pawn never even considers the player as a
	// candidate -- which would explain a hostile NPC ignoring a target its
	// AICanSee() reports at full visibility.
	foreach Target.VisibleActors( class'Actor', A, 2000 )
	{
		nVisible++;
		if ( A.IsA('Pawn') )
			nVisiblePawns++;
		if ( A == Player )
			bPlayerVisible = True;
	}

	Emit("V"$Chr(9)$Seq
		$Chr(9)$Stamp()
		$Chr(9)$TeleportTargetName
		$Chr(9)$nVisible
		$Chr(9)$nVisiblePawns
		$Chr(9)$bPlayerVisible);

	Emit("P"$Chr(9)$Seq
		$Chr(9)$Stamp()
		$Chr(9)$TeleportTargetName
		$Chr(9)$Player.AIVisibility()
		$Chr(9)$Target.AICanSee(Player)
		$Chr(9)$Target.AICanSee(Player, 1.0)
		$Chr(9)$int(VSize(Target.Location - Player.Location))
		$Chr(9)$string(Target.GetStateName())
		$Chr(9)$Target.CanSee(Player)
		$Chr(9)$Target.LineOfSightTo(Player)
		$Chr(9)$Player.bIsPlayer
		$Chr(9)$Target.SightRadius
		$Chr(9)$Target.PeripheralVision
		$Chr(9)$Target.AICanSee(Player, 1.0, False)
		$Chr(9)$Player.Region.Zone.AmbientBrightness
		$Chr(9)$Player.CollisionRadius
		$Chr(9)$Player.CollisionHeight);

	// AIGetLightLevel is what scales AICanSee's visibility term, and SurrealEngine
	// returns a flat 1.0 for it. Sampled at both pawns' positions, plus a point well
	// above the player, so retail's values can be correlated against location and
	// against the zone ambient that turned out not to be a usable stand-in.
	Emit("L"$Chr(9)$Seq
		$Chr(9)$Stamp()
		$Chr(9)$Target.AIGetLightLevel(Player.Location)
		$Chr(9)$Target.AIGetLightLevel(Target.Location)
		$Chr(9)$Target.AIGetLightLevel(Player.Location + vect(0,0,128))
		$Chr(9)$Player.Region.Zone.AmbientBrightness
		$Chr(9)$Player.Location
		$Chr(9)$string(Player.Region.Zone.Name));

	// Fixed world points, so the two engines are compared at identical coordinates.
	// Sampling at the pawns' own positions cannot be compared across runs: the
	// player's path differs every run, which moves the whole distribution.
	Emit("Q"$Chr(9)$Seq
		$Chr(9)$Stamp()
		$Chr(9)$Target.AIGetLightLevel(vect(-2176,-777,-92))
		$Chr(9)$Target.AIGetLightLevel(vect(-2300,-700,-92))
		$Chr(9)$Target.AIGetLightLevel(vect(-2100,-850,-92))
		$Chr(9)$Target.AIGetLightLevel(vect(-2176,-777,36))
		$Chr(9)$Target.AIGetLightLevel(vect(-1800,-600,-92)));
	Sink.Flush();
}

// Walks the gates the Attacking state passes through between acquiring an enemy
// and pulling the trigger, so a stalled fire loop can be attributed to a
// specific term rather than to combat "feeling slow".
// ComputeBestFiringPosition derives its range band from GetPawnWeaponRanges
// called twice -- once for the pawn, once for its enemy -- and only leaves the
// pawn standing still to shoot when the current distance already falls inside
// that band. Emitted as its own row rather than appended to the W row, which is
// already at the concatenation length that hangs SurrealEngine.
// GetShotTime() lives inside state NormalFire so it cannot be called from here.
// It returns ShotTime*(BaseAccuracy*2+1) when the owner casts to ScriptedPawn
// and ShotTime otherwise, so log the branch condition instead.
function string OwnerIsScriptedPawn( DeusExWeapon W )
{
	if ( W == None )
		return "noweapon";
	if ( ScriptedPawn(W.Owner) != None )
		return "SP";
	return "notSP";
}

// Tick() is declared outside any state, so it keeps running while this state
// holds the latent Sleep.
state SleepTester
{
Begin:
	SleepT0 = Level.TimeSeconds;
	Sleep(2.0);
	Emit("#"$Chr(9)$"sleeptest"$Chr(9)$"asked"$Chr(9)$"2.000000"
		$Chr(9)$"measured"$Chr(9)$(Level.TimeSeconds - SleepT0));
	Sink.Flush();
	Goto('Begin');
}

// Population census of every living ScriptedPawn's weapon. SampleTransitions watches
// only the one named pawn, so a pawn disarmed anywhere else in the level is invisible
// to it. An engine that drops weapons mid-combat shows up here as armed falling below
// alive while the pawns are still fighting.
function SampleWeaponCensus()
{
	local Pawn P;
	local ScriptedPawn SP;
	local int Alive, Armed, Fighting, NoAmmo;
	local string Roster;
	local string Combatants;

	for ( P = Level.PawnList; P != None; P = P.NextPawn )
	{
		SP = ScriptedPawn(P);
		if ( SP == None || SP.Health <= 0 )
			continue;

		Alive++;
		if ( SP.Weapon != None )
		{
			Armed++;
			if ( SP.Weapon.AmmoType != None && SP.Weapon.AmmoType.AmmoAmount <= 0 )
				NoAmmo++;
		}
		else
			Roster = Roster$" "$string(SP.Name);
		if ( SP.Enemy != None )
		{
			Fighting++;
			Combatants = Combatants$" "$string(SP.Name)$">"$string(SP.Enemy.Name);
		}
	}

	if ( Alive == PrevAlive && Armed == PrevArmed && Fighting == PrevFighting )
		return;

	Emit("C"$Chr(9)$Seq$Chr(9)$Stamp()
		$Chr(9)$Alive$Chr(9)$Armed$Chr(9)$Fighting$Chr(9)$NoAmmo);

	// The counts say a pawn was disarmed but not which one, so pair every census
	// row with the roster of living unarmed pawns. Diffing consecutive rosters
	// names the pawn that lost its weapon.
	Emit("N"$Chr(9)$Seq$Chr(9)$Stamp()$Chr(9)$Roster);
	// Who each fighting pawn is actually fighting, to tell a wider alliance
	// broadcast apart from more pawns independently spotting the player.
	Emit("F"$Chr(9)$Seq$Chr(9)$Stamp()$Chr(9)$Combatants);
	Sink.Flush();

	PrevAlive = Alive;
	PrevArmed = Armed;
	PrevFighting = Fighting;
}

// A concatenation chain long enough to exceed the bytecode reader's nesting limit.
// Retail compiles and runs it; SurrealEngine has to as well, so this row doubles as
// the regression check for that limit. The expected value is the digits 1..48 in
// order, separated by dots.
function SampleLongExpr()
{
	Emit("#"$Chr(9)$"longexpr"$Chr(9)
		$"1"$"."$"2"$"."$"3"$"."$"4"$"."$"5"$"."$"6"$"."$"7"$"."$"8"
		$"."$"9"$"."$"10"$"."$"11"$"."$"12"$"."$"13"$"."$"14"$"."$"15"
		$"."$"16"$"."$"17"$"."$"18"$"."$"19"$"."$"20"$"."$"21"$"."$"22"
		$"."$"23"$"."$"24"$"."$"25"$"."$"26"$"."$"27"$"."$"28"$"."$"29"
		$"."$"30"$"."$"31"$"."$"32"$"."$"33"$"."$"34"$"."$"35"$"."$"36"
		$"."$"37"$"."$"38"$"."$"39"$"."$"40"$"."$"41"$"."$"42"$"."$"43"
		$"."$"44"$"."$"45"$"."$"46"$"."$"47"$"."$"48");
}

// Pawn.uc documents LastSeenPos/LastSeeingPos as engine-maintained while
// EnemyNotVisible is enabled, and ScriptedPawn's Seeking state reads LastSeenPos
// in BeginState to choose where to search. Sampled so the two engines' values can
// be diffed directly.
function SampleSeek()
{
	local Pawn P;
	local ScriptedPawn Target;

	for ( P = Level.PawnList; P != None; P = P.NextPawn )
		if ( string(P.Name) == TeleportTargetName )
			Target = ScriptedPawn(P);

	if ( Target == None )
		return;

	Emit("S"$Chr(9)$Seq$Chr(9)$Stamp()$Chr(9)$TeleportTargetName
		$Chr(9)$Target.GetPropertyText("LastSeenPos")
		$Chr(9)$Target.GetPropertyText("LastSeeingPos")
		$Chr(9)$Target.EnemyLastSeen
		$Chr(9)$string(Target.Enemy)
		$Chr(9)$string(Target.GetStateName())
		$Chr(9)$Target.bSeekLocation
		$Chr(9)$VSize(Target.LastSeenPos - Target.Location)
		// ElapsedTime sums the DeltaTime handed to Tick; Stamp() reads
		// Level.TimeSeconds. They are the same clock in SurrealEngine but not in
		// retail, which makes every millisecond figure engine-relative until the
		// ratio is known.
		$Chr(9)$ElapsedTime
		$Chr(9)$TickN
		// LevelInfo's Minute/Second/Millisecond are the real-world clock, so they
		// arbitrate between the other two.
		$Chr(9)$(Level.Minute*60 + Level.Second + Level.Millisecond*0.001));
	Sink.Flush();
}

function SampleTransitions()
{
	local Pawn P;
	local ScriptedPawn Target;
	local DeusExWeapon W;
	local int Ammo;
	local int ClipC;
	local int ReloadC;
	local bool bReady;
	local string WS;

	for ( P = Level.PawnList; P != None; P = P.NextPawn )
		if ( string(P.Name) == TeleportTargetName )
			Target = ScriptedPawn(P);

	if ( Target == None )
		return;

	if ( ForceBaseAccuracy != "" )
	{
		Target.SetPropertyText( "BaseAccuracy", ForceBaseAccuracy );
		if ( !bAccuracyForced )
		{
			bAccuracyForced = True;
			Emit("#"$Chr(9)$"forceBaseAccuracy"$Chr(9)$ForceBaseAccuracy
				$Chr(9)$Target.GetPropertyText("BaseAccuracy"));
			Sink.Flush();
		}
	}

	W = DeusExWeapon(Target.Weapon);
	if ( W != None )
	{
		bReady = W.bReadyToFire;
		WS = string(W.GetStateName());
		ClipC = W.ClipCount;
		ReloadC = W.ReloadCount;
		if ( W.AmmoType != None )
			Ammo = W.AmmoType.AmmoAmount;
	}

	if ( Target.AnimSequence == PrevAnim && Ammo == PrevAmmo
		&& bReady == PrevReady && WS == PrevWeaponState && ClipC == PrevClip )
		return;

	Emit("T"$Chr(9)$Seq$Chr(9)$Stamp()$Chr(9)$TeleportTargetName
		$Chr(9)$string(Target.AnimSequence)
		$Chr(9)$Target.AnimRate
		$Chr(9)$Ammo
		$Chr(9)$bReady
		$Chr(9)$WS
		$Chr(9)$string(Target.GetStateName())
		$Chr(9)$OwnerIsScriptedPawn(W)
		// AmmoAmount is the reserve pool, so on its own it cannot distinguish a
		// shot from a reload drawing rounds out of reserve.
		$Chr(9)$ClipC
		$Chr(9)$ReloadC);
	Sink.Flush();

	PrevAnim = Target.AnimSequence;
	PrevAmmo = Ammo;
	PrevReady = bReady;
	PrevWeaponState = WS;
	PrevClip = ClipC;
}

function SampleRanges()
{
	local Pawn P, Player;
	local ScriptedPawn Target;
	local float sMin, sAcc, sMax;
	local float eMin, eAcc, eMax;

	for ( P = Level.PawnList; P != None; P = P.NextPawn )
	{
		if ( P.bIsPlayer && Player == None )
			Player = P;
		if ( string(P.Name) == TeleportTargetName )
			Target = ScriptedPawn(P);
	}

	if ( Player == None || Target == None )
		return;

	Target.GetPawnWeaponRanges( Target, sMin, sAcc, sMax );
	Target.GetPawnWeaponRanges( Player, eMin, eAcc, eMax );

	Emit("R"$Chr(9)$Seq$Chr(9)$Stamp()$Chr(9)$TeleportTargetName
		$Chr(9)$string(Player.Weapon)
		$Chr(9)$sMin $Chr(9)$sAcc $Chr(9)$sMax
		$Chr(9)$eMin $Chr(9)$eAcc $Chr(9)$eMax
		$Chr(9)$Target.CollisionRadius
		$Chr(9)$Player.CollisionRadius
		$Chr(9)$Target.GroundSpeed
		// bUseSprint is the only term left that can force a reposition here, and
		// it is decided by FRand() <= SprintRate*0.5. Raw draws so the two
		// engines' distributions can be compared directly.
		$Chr(9)$FRand() $Chr(9)$FRand() $Chr(9)$FRand() $Chr(9)$FRand()
		// PickDestination runs once per latent tick while the weapon cools down,
		// and each call re-rolls the sprint chance, so the strafe rate per second
		// scales with tick rate rather than with anything in the AI itself.
		$Chr(9)$(TickN - LastTickN) $Chr(9)$(Level.TimeSeconds - LastTickTime)
		// Actual travel speed against GroundSpeed: a strafe destination is only
		// 100-300 units out, so 4.4s parked in Strafe2H means the pawn is not
		// covering ground at the speed the AI assumes it does.
		$Chr(9)$VSize(Target.Velocity)
		$Chr(9)$VSize(Target.Location - LastTargetLoc)
		$Chr(9)$Target.Physics);
	LastTargetLoc = Target.Location;
	LastTickN = TickN;
	LastTickTime = Level.TimeSeconds;
	Sink.Flush();
}

function SampleWeapon()
{
	local Pawn P, Player;
	local ScriptedPawn Target;
	local DeusExWeapon W;
	local int ammo;

	for ( P = Level.PawnList; P != None; P = P.NextPawn )
	{
		if ( P.bIsPlayer && Player == None )
			Player = P;
		if ( string(P.Name) == TeleportTargetName )
			Target = ScriptedPawn(P);
	}

	if ( Player == None || Target == None )
		return;

	W = DeusExWeapon( Target.Weapon );
	if ( W == None )
	{
		Emit("W"$Chr(9)$Seq$Chr(9)$Stamp()$Chr(9)$TeleportTargetName$Chr(9)$"noweapon");
		Sink.Flush();
		return;
	}

	if ( W.AmmoType != None )
		ammo = W.AmmoType.AmmoAmount;
	else
		ammo = -1;

	Emit("W"$Chr(9)$Seq$Chr(9)$Stamp()$Chr(9)$TeleportTargetName
		$Chr(9)$string(W.Class)
		$Chr(9)$W.bReadyToFire
		$Chr(9)$Target.ReloadTimer
		$Chr(9)$Target.FireTimer
		$Chr(9)$W.AIFireDelay
		$Chr(9)$ammo
		$Chr(9)$W.ReloadCount
		$Chr(9)$W.ClipCount
		$Chr(9)$Target.AICanShoot( Player, True, False, 0.025 )
		$Chr(9)$Target.AICanShoot( Player, True, True, 0.025 )
		$Chr(9)$Target.bCanFire
		$Chr(9)$Target.bFacingTarget
		$Chr(9)$Target.bReadyToReload
		$Chr(9)$Target.MinRange
		$Chr(9)$Target.MaxRange
		$Chr(9)$string(Target.AnimSequence)
		$Chr(9)$Target.AnimFrame
		$Chr(9)$Target.AnimRate
		$Chr(9)$Target.bFire
		// The automatic path in DeusExWeapon.NormalFire loops on Goto('Begin')
		// and never reaches ReadyToFire(), so bAutomatic plus the live state
		// name separates "read the flag wrong" from "left the loop early".
		$Chr(9)$W.bAutomatic
		$Chr(9)$W.bFiring
		$Chr(9)$W.ShotTime
		$Chr(9)$string(W.GetStateName())
		$Chr(9)$W.bInstantHit
		$Chr(9)$W.HitDamage
		$Chr(9)$W.AccurateRange
		$Chr(9)$Target.BaseAccuracy
		// The spread TraceFire actually applies. Pure computation, so reading it
		// here does not disturb the shot it describes.
		$Chr(9)$W.CalculateAccuracy()
		$Chr(9)$W.GetWeaponSkill()
		$Chr(9)$VSize(Target.Location - W.Owner.Location));
	Sink.Flush();
}

// Evaluates, term by term, the exact expression ScriptedPawn.CheckEnemyPresence
// uses to decide whether the player is worth attacking. Any term that reads zero
// or false here is the reason the pawn stays idle.
function SampleEnemyGate()
{
	local Pawn P, Player;
	local ScriptedPawn Target;
	local float vis;

	for ( P = Level.PawnList; P != None; P = P.NextPawn )
	{
		if ( P.bIsPlayer && Player == None )
			Player = P;
		if ( string(P.Name) == TeleportTargetName )
			Target = ScriptedPawn(P);
	}

	if ( Player == None || Target == None )
	{
		// Emitted rather than skipped so a gap in the G rows reads as "the pawn
		// left the level" instead of "the sampler stopped".
		Emit("G"$Chr(9)$Seq$Chr(9)$Stamp()$Chr(9)$TeleportTargetName$Chr(9)$"gone");
		Sink.Flush();
		return;
	}

	vis = Target.ComputeActorVisibility( Player );

	Emit("G"$Chr(9)$Seq$Chr(9)$Stamp()$Chr(9)$TeleportTargetName
		$Chr(9)$Target.IsValidEnemy( Player )
		$Chr(9)$Target.IsValidEnemy( Player, False )
		$Chr(9)$vis
		$Chr(9)$Target.AICanSee( Player, vis, True, True, True, True )
		$Chr(9)$Target.DistanceFromPlayer
		$Chr(9)$Target.LastRendered()
		$Chr(9)$Target.bTickVisibleOnly
		$Chr(9)$Target.EnemyReadiness
		$Chr(9)$Target.CycleCumulative
		$Chr(9)$string(Target.CycleCandidate)
		$Chr(9)$string(Player.Alliance)
		$Chr(9)$int(Target.GetPawnAllianceType( Player ))
		$Chr(9)$int(Target.GetAllianceType( Player.Alliance ))
		$Chr(9)$int(Target.GetAllianceType( 'Player' ))
		$Chr(9)$Target.bReverseAlliances
		$Chr(9)$Target.bLikesNeutral
		$Chr(9)$Player.Health
		$Chr(9)$Target.Health
		$Chr(9)$Target.EnemyLastSeen
		$Chr(9)$Target.EnemyTimeout
		$Chr(9)$string(Target.Enemy));
	SampleDamageReduction( Player );
	SampleArmedPawns( Player );
	SampleTraceActors( Player, Target );
	SamplePatrol();
	SamplePawnGenerators();
	Sink.Flush();
}

// PawnGenerator picks a spawn point with Scout.AIPickRandomDestination(0, dist,
// 0, 0, 0, 0, 2, 1.0, destination) and gives up after two tries, so how that
// native spreads its candidates decides how many animals a map gets. Pigeons go
// through a walking scout and seagulls through a flying one, and the two engines
// disagree about which of those succeeds -- so run the identical call from a
// scout of our own, varying nothing but Physics.
// Holds the player just inside the named generator's ActiveArea and facing away
// from it, then counts what it produces. PawnGenerator.Tick() refuses to generate
// while IsActorUnnecessary(self) holds, so nothing at all spawns until the player
// is this close; facing away keeps PlayerCanSeeActor from vetoing every candidate.
function TickPopulationProbe(float DeltaTime)   // PC and PN rows
{
	local PawnGenerator PG;
	local Pawn   Player;
	local Actor  A;
	local ScriptedPawn SP;
	local vector Dir;
	local int    i, nPigeon, nSeagull, nRat, nFly, nFish;
	local Pawn   Cand, Startler;
	local Actor  NoiseSource;
	local float  CandSpeed, CandTime, StartlerSpeed;

	Player = Level.Game.GetPlayerPawn();
	if ( Player == None )
		return;

	if ( PopGen == None )
	{
		foreach AllActors(class'PawnGenerator', PG)
			if ( string(PG.Name) == PopGenerator )
			{
				PopGen = PG;
				break;
			}
		if ( PopGen == None )
			return;
	}

	if ( !bPopPlaced )
	{
		// Any direction will do so long as the player fits there; the four axes are
		// tried in turn so a generator backed against a wall still gets a viewpoint.
		for ( i = 0; i < 4; i++ )
		{
			if ( i == 0 )      Dir = vect(1,0,0);
			else if ( i == 1 ) Dir = vect(-1,0,0);
			else if ( i == 2 ) Dir = vect(0,1,0);
			else               Dir = vect(0,-1,0);

			if ( Player.SetLocation(PopGen.Location + Dir * PopOffset) )
			{
				PopPlayerLoc = Player.Location;
				if ( bPopFaceGenerator )
					PopPlayerRot = rotator(-Dir);
				else
					PopPlayerRot = rotator(Dir);
				bPopPlaced   = True;
				break;
			}
		}
		if ( !bPopPlaced )
			return;
		Emit("#"$Chr(9)$"popprobe"$Chr(9)$PopGenerator
			$Chr(9)$"activeArea="$PopGen.ActiveArea
			$Chr(9)$"radius="$PopGen.Radius
			$Chr(9)$"maxCount="$PopGen.MaxCount
			$Chr(9)$"pool="$PopGen.PoolCount
			$Chr(9)$"offset="$PopOffset
			$Chr(9)$"dist="$VSize(Player.Location - PopGen.Location));
	}

	// Re-asserted every tick: the player is still a physics actor and would
	// otherwise fall or drift out of range and stop the generator again.
	Player.SetLocation(PopPlayerLoc);
	Player.SetRotation(PopPlayerRot);
	Player.Velocity = vect(0,0,0);

	if ( NoiseProbePeriod > 0 )
	{
		NoiseAccum += DeltaTime;
		// Put the birds back a second early. GotoState from outside the pawn does
		// not necessarily take effect before its next tick, and a bird that has not
		// re-entered Wandering has not re-registered its LoudNoise callback, so
		// resetting in the same frame as the event reads as a refused delivery.
		if ( bNoiseProbeReset && !bNoiseResetDone && NoiseAccum >= NoiseProbePeriod - 1.0 )
		{
			bNoiseResetDone = True;
			foreach AllActors(class'ScriptedPawn', SP)
				if ( (SP.IsA('Pigeon') || SP.IsA('Seagull') || SP.IsA('Rat')) && SP.GetStateName() != 'Wandering' )
					SP.GotoState('Wandering');
		}
		if ( NoiseAccum >= NoiseProbePeriod )
		{
			NoiseAccum = 0;
			bNoiseResetDone = False;
			// A negative distance parks the emitter on the player, a spot both
			// engines have already agreed is open ground, so a refusal there is
			// about the source actor rather than where it was put.
			if ( NoiseProbeDist < 0 )
				SetLocation(Player.Location);
			else
				SetLocation(PopGen.Location + vect(1,0,0)*NoiseProbeDist);
			if ( bNoiseProbeFromPlayer )
				NoiseSource = Player;
			else
				NoiseSource = Self;
			foreach AllActors(class'ScriptedPawn', SP)
			{
				if ( !SP.IsA('Pigeon') && !SP.IsA('Seagull') && !SP.IsA('Rat') )
					continue;
				Emit("NP"$Chr(9)$Seq$Chr(9)$Stamp()
					$Chr(9)$Level.TimeSeconds
					$Chr(9)$SP.Name
					$Chr(9)$VSize(SP.Location - NoiseSource.Location)
					$Chr(9)$NoiseProbeRadius
					$Chr(9)$NoiseProbeVolume
					$Chr(9)$SP.GetStateName()
					$Chr(9)$NoiseSource.Name
					$Chr(9)$SP.FastTrace(NoiseSource.Location, SP.Location)
					$Chr(9)$SP.FastTrace(NoiseSource.Location + vect(0,0,32), SP.Location));
			}
			NoiseSource.AISendEvent('LoudNoise', EAITYPE_Audio, NoiseProbeVolume, NoiseProbeRadius);
		}
	}

	PopAccum += DeltaTime;
	if ( PopAccum < PopCensusPeriod )
		return;
	PopAccum = 0;

	foreach AllActors(class'Actor', A)
	{
		if ( A.bDeleteMe )
			continue;
		if ( A.IsA('Pigeon') )       nPigeon++;
		else if ( A.IsA('Seagull') ) nSeagull++;
		else if ( A.IsA('Rat') )     nRat++;
		else if ( A.IsA('Fly') )     nFly++;
		else if ( A.IsA('Fish') )    nFish++;
	}

	Emit("PN"$Chr(9)$Seq$Chr(9)$Stamp()
		$Chr(9)$Level.TimeSeconds
		$Chr(9)$nPigeon $Chr(9)$nSeagull $Chr(9)$nRat $Chr(9)$nFly $Chr(9)$nFish
		$Chr(9)$VSize(Player.Location - PopGen.Location)
		$Chr(9)$VSize(Player.Velocity)$Chr(9)$Player.Physics$Chr(9)$Player.bIsWalking);

	// ScriptedPawn.PlayFootStep sends LoudNoise with a range of up to 2048, and a
	// bird that hears one flees into Flying. Log every pawn close enough for its
	// footsteps to carry, with the mass the range is derived from.
	foreach PopGen.RadiusActors(class'Pawn', Cand, 2560)
		Emit("NZ"$Chr(9)$Seq$Chr(9)$Stamp()
			$Chr(9)$Level.TimeSeconds
			$Chr(9)$Cand.Name
			$Chr(9)$Cand.Class.Name
			$Chr(9)$VSize(Cand.Location - PopGen.Location)
			$Chr(9)$VSize(Cand.Velocity)
			$Chr(9)$Cand.Mass
			$Chr(9)$Cand.Physics
			$Chr(9)$Cand.GetStateName());

	// CheckPawnStatus() culls a transient pawn when IsActorUnnecessary() and
	// !PlayerCanSeeActor() both hold, i.e. DistanceFromPlayer >= ActiveArea and
	// LastRendered() > 5. Both terms are logged so it is clear which one differs.
	foreach AllActors(class'ScriptedPawn', SP)
	{
		if ( !SP.IsA('Pigeon') && !SP.IsA('Seagull') && !SP.IsA('Rat') )
			continue;
		// Animal.state Wandering.Tick polls FrightenedByPawn() twice a second, and a
		// bird that finds one flees into state Flying. Replayed here so a capture
		// shows whether an engine's birds are being startled or are entering Flying
		// through the LoudNoise callback instead.
		Startler = None;
		StartlerSpeed = 0;
		foreach SP.RadiusActors(class'Pawn', Cand, 500)
		{
			if ( Cand == SP || ClassIsChildOf(Cand.Class, SP.Class) || !Cand.bBlockActors )
				continue;
			CandSpeed = VSize(Cand.Velocity);
			if ( CandSpeed < 20 )
				continue;
			CandTime = VSize(SP.Location - Cand.Location) / CandSpeed;
			if ( CandTime > 2.0 )
				continue;
			if ( VSize(SP.Location - (Cand.Location + Cand.Velocity*CandTime)) < CandSpeed*0.6 )
			{
				Startler = Cand;
				StartlerSpeed = CandSpeed;
				break;
			}
		}

		Emit("PB"$Chr(9)$Seq$Chr(9)$Stamp()
			$Chr(9)$Level.TimeSeconds
			$Chr(9)$SP.Name
			$Chr(9)$SP.DistanceFromPlayer
			$Chr(9)$VSize(SP.Location - Player.Location)
			$Chr(9)$SP.LastRendered()
			$Chr(9)$VSize(SP.Location - PopGen.Location)
			$Chr(9)$SP.Location.Z
			$Chr(9)$SP.GetStateName()
			$Chr(9)$SP.Physics
			$Chr(9)$SP.Health
			$Chr(9)$SP.bHidden
			// ScriptedPawn's Wandering state code runs GoHome() before wandering,
			// and GoHome() is gated on bUseHome && !IsNearHome(Location). Both of
			// IsNearHome's terms are reported so it is clear which one lets a bird
			// keep drifting: the extent test or the line of sight back to home.
			$Chr(9)$SP.bUseHome
			$Chr(9)$SP.HomeExtent
			$Chr(9)$VSize(SP.Location - SP.HomeLoc)
			$Chr(9)$SP.FastTrace(SP.HomeLoc, SP.Location)
			$Chr(9)$SP.PointReachable(SP.HomeLoc)
			$Chr(9)$SP.bDefendHome
			$Chr(9)$SP.Wanderlust
			// UE1 stops ticking a bStasis actor that has not been rendered lately.
			// If retail freezes the birds and SurrealEngine keeps simulating them,
			// that alone explains why only SurrealEngine's drift out of range.
			$Chr(9)$SP.bStasis
			$Chr(9)$SP.bTickVisibleOnly
			$Chr(9)$SP.Velocity.X$Chr(9)$SP.Velocity.Y$Chr(9)$SP.Velocity.Z
			$Chr(9)$SP.Acceleration.X$Chr(9)$SP.Acceleration.Y
			$Chr(9)$SP.Region.ZoneNumber$Chr(9)$Player.Region.ZoneNumber
			$Chr(9)$SP.Region.Zone.Name$Chr(9)$Player.Region.Zone.Name
			$Chr(9)$Animal(SP).bFleeBigPawns
			$Chr(9)$SP.bBlockActors$Chr(9)$SP.bBlockPlayers
			$Chr(9)$string(Startler.Name)$Chr(9)$StartlerSpeed);
	}

	foreach AllActors(class'PawnGenerator', PG)
		Emit("PC"$Chr(9)$Seq$Chr(9)$Stamp()
			$Chr(9)$PG.Name
			$Chr(9)$PG.PawnCount
			$Chr(9)$PG.PoolCount
			$Chr(9)$PG.MaxCount
			$Chr(9)$PG.bDying
			$Chr(9)$PG.DistanceFromPlayer
			$Chr(9)$PG.ActiveArea);

	Sink.Flush();
}

// PawnGenerator.GeneratePawn() replayed against every real generator in the map,
// with each of its three gates reported separately: the destination pick, the
// player-visibility test and the distance test. Only the map generators matter --
// Burst() bypasses all three, and nothing but FlyGenerator ever calls it.
function DumpScoutPick()   // SC rows
{
	local PawnGenerator Gens[24];
	local PawnGenerator PG;
	local Pawn   Scout, Player;
	local class<ScriptedPawn> PC;
	local vector GroundLoc, StartLoc, HitLoc, HitNorm, Dest, PlayerLoc;
	local int    i, g, NumGens;
	local float  Dist;
	local bool   bOk, bCanSee, bUnnec;
	local EPhysics NewPhys;

	if ( ScoutPickTrials <= 0 )
		return;

	Player = Level.Game.GetPlayerPawn();
	if ( Player != None )
		PlayerLoc = Player.Location;

	foreach AllActors(class'PawnGenerator', PG)
	{
		if ( NumGens >= 24 )
			break;
		Gens[NumGens] = PG;
		NumGens++;
	}

	for ( g = 0; g < NumGens; g++ )
	{
		PG = Gens[g];
		PC = PG.PawnClasses[0].PawnClass;
		if ( PC == None )
			continue;

		NewPhys = PC.Default.Physics;
		if ( (NewPhys == PHYS_None) || (NewPhys == PHYS_Falling) )
			NewPhys = PHYS_Walking;

		if ( Trace(HitLoc, HitNorm, PG.Location - vect(0,0,200), PG.Location, false) != None )
			GroundLoc = HitLoc;
		else
		{
			GroundLoc    = PG.Location;
			GroundLoc.Z -= PG.CollisionHeight;
		}

		if ( NewPhys == PHYS_Walking )
			StartLoc = GroundLoc + vect(0,0,1) * PC.Default.CollisionHeight;
		else
			StartLoc = PG.Location;

		Scout = Spawn(class'GeneratorScout', , , StartLoc);
		if ( Scout == None )
			continue;
		Scout.SetCollision(false, false, false);
		Scout.Health   = 100;
		Scout.bHidden  = True;

		Emit("#"$Chr(9)$"scoutgen"$Chr(9)$PG.Name$Chr(9)$PC.Name
			$Chr(9)$"physics="$NewPhys
			$Chr(9)$"radius="$PG.Radius
			$Chr(9)$"activeArea="$PG.ActiveArea
			$Chr(9)$"collR="$PC.Default.CollisionRadius
			$Chr(9)$"collH="$PC.Default.CollisionHeight
			$Chr(9)$"startZ="$StartLoc.Z
			$Chr(9)$"genZ="$PG.Location.Z
			$Chr(9)$"playerDist="$VSize(PG.Location - PlayerLoc));

		for ( i = 0; i < ScoutPickTrials; i++ )
		{
			// The shrink/move/restore dance matters: the scout is placed at 5x5 so
			// it cannot be pushed out of a tight spot, then grown to the real pawn's
			// cylinder before the destination search runs against it.
			Scout.bCollideWorld = False;
			Scout.SetCollisionSize(5, 5);
			Scout.SetLocation(StartLoc);
			Scout.SetCollisionSize(PC.Default.CollisionRadius, PC.Default.CollisionHeight);
			Scout.SetPhysics(NewPhys);
			Scout.bCollideWorld = True;

			if ( ScoutPickMaxDist > 0 )
				Dist = ScoutPickMaxDist;
			else
			{
				Dist = Sqrt(FRand()) * PG.Radius;
				if ( Dist < PC.Default.CollisionRadius * 2 + 1 )
					Dist = PC.Default.CollisionRadius * 2 + 1;
			}

			Dest    = vect(0,0,0);
			bCanSee = False;
			bUnnec  = False;
			bOk     = Scout.AIPickRandomDestination(0, Dist, 0, 0, 0, 0, 2, 1.0, Dest);
			if ( bOk )
			{
				Scout.SetLocation(Dest);
				Scout.bHidden     = False;
				Scout.bDetectable = True;
				if ( Player != None )
					bCanSee = Player.AICanSee(Scout, 1.0, False, False, True, True) > 0.0;
				bUnnec = Scout.DistanceFromPlayer >= PG.ActiveArea;
				Scout.bHidden     = True;
				Scout.bDetectable = False;
			}

			Emit("SC"$Chr(9)$Seq
				$Chr(9)$Stamp()
				$Chr(9)$PG.Name
				$Chr(9)$PC.Name
				$Chr(9)$i
				$Chr(9)$Dist
				$Chr(9)$bOk
				$Chr(9)$bCanSee
				$Chr(9)$bUnnec
				$Chr(9)$(Dest.Z - StartLoc.Z)
				$Chr(9)$VSize((Dest - StartLoc) * vect(1,1,0))
				$Chr(9)$Scout.DistanceFromPlayer
				$Chr(9)$VSize(Dest - PlayerLoc)
				$Chr(9)$NewPhys);
		}

		Scout.Destroy();
		Sink.Flush();
	}
}

// The whole actor inventory, once. SurrealEngine turned out to be missing a
// map-placed FlyGenerator with nothing in its log to say so, and a set diff
// against retail is the only way to find out what else never got created.
function DumpActorList()   // AL rows
{
	local Actor A;
	local int   N;

	foreach AllActors(class'Actor', A)
	{
		N++;
		Emit("AL"$Chr(9)$A.Name
			$Chr(9)$string(A.Class)
			$Chr(9)$A.Location.X
			$Chr(9)$A.Location.Y
			$Chr(9)$A.Location.Z
			$Chr(9)$A.bHidden
			$Chr(9)$A.bStatic
			$Chr(9)$A.bNoDelete
			$Chr(9)$A.bCollideActors
			$Chr(9)$A.Physics);
	}
	Emit("#"$Chr(9)$"actorlist"$Chr(9)$"count="$N);
}

// Retail spawns three Pigeons on the docks where SurrealEngine spawns a Seagull
// and an extra Rat. PawnGenerator.GeneratePawn() only commits after PoolCount,
// AIPickRandomDestination, PlayerCanSeeActor and IsActorUnnecessary all agree,
// so the counters say which generator is trying and how far it gets.
function SamplePawnGenerators()   // PG rows
{
	local Actor A;

	foreach AllActors(class'Actor', A)
	{
		if ( !A.IsA('PawnGenerator') )
			continue;

		Emit("PG"$Chr(9)$Seq
			$Chr(9)$Stamp()
			$Chr(9)$A.Name
			$Chr(9)$string(A.Class)
			$Chr(9)$A.GetPropertyText("PawnCount")
			$Chr(9)$A.GetPropertyText("MaxCount")
			$Chr(9)$A.GetPropertyText("PoolCount")
			$Chr(9)$A.GetPropertyText("Frequency")
			$Chr(9)$A.GetPropertyText("Radius")
			$Chr(9)$A.GetPropertyText("bDying")
			$Chr(9)$A.GetPropertyText("Scout")
			$Chr(9)$A.GetPropertyText("bRandomTypes")
			$Chr(9)$A.GetPropertyText("bRepopulate")
			$Chr(9)$A.GetPropertyText("PawnClasses")
			$Chr(9)$A.GetStateName());
	}
}

// One row per pawn per frame for the first fraction of a second. Whether a pawn
// ends up resting at its default cylinder height or at the shrunken one depends
// on the order the fall, the landing and InitializePawn()'s SetCollisionSize
// happen in, and all three happen within the first few frames.
function SampleEarlyBurst( float DeltaTime )   // EB rows
{
	local Pawn P;
	local ScriptedPawn SP;

	EarlyBurstFrame++;
	for ( P = Level.PawnList; P != None; P = P.NextPawn )
	{
		SP = ScriptedPawn(P);
		if ( SP == None )
			continue;

		Emit("EB"$Chr(9)$Seq
			$Chr(9)$Stamp()
			$Chr(9)$EarlyBurstFrame
			$Chr(9)$DeltaTime
			$Chr(9)$Level.TimeSeconds
			$Chr(9)$SP.Name
			$Chr(9)$SP.Location.Z
			$Chr(9)$SP.Velocity.Z
			$Chr(9)$SP.CollisionHeight
			$Chr(9)$SP.Default.CollisionHeight
			$Chr(9)$SP.Physics
			$Chr(9)$SP.GetStateName()
			$Chr(9)$SP.bInWorld
			$Chr(9)$SP.PrePivotOffset.Z
			$Chr(9)$SP.Base);
	}
	Sink.Flush();
}

// Where each patrolling pawn is heading and how fast. A pawn that walks a route
// and one that walks away in a straight line both report state Patrolling, so the
// state name alone cannot tell them apart -- destPoint and velocity can.
function SamplePatrol()   // PT rows
{
	local Pawn P;
	local ScriptedPawn SP;
	local vector HitLoc, HitNorm;
	local float FloorDist;

	for ( P = Level.PawnList; P != None; P = P.NextPawn )
	{
		SP = ScriptedPawn(P);
		if ( SP == None )
			continue;

		// How far the pawn's origin sits above the world surface under it. The two
		// engines disagree on resting height while reporting identical collision
		// sizes, so the question is which of the floor or the pawn moved.
		if ( Trace(HitLoc, HitNorm, SP.Location - vect(0,0,400), SP.Location, false) != None )
			FloorDist = SP.Location.Z - HitLoc.Z;
		else
			FloorDist = -1;

		Emit("PT"$Chr(9)$Seq$Chr(9)$Stamp()
			$Chr(9)$string(SP.Name)
			$Chr(9)$string(SP.GetStateName())
			$Chr(9)$string(SP.Orders)
			$Chr(9)$string(SP.OrderTag)
			$Chr(9)$string(SP.destPoint)
			$Chr(9)$string(SP.MoveTarget)
			$Chr(9)$SP.Location.X$Chr(9)$SP.Location.Y$Chr(9)$SP.Location.Z
			$Chr(9)$VSize(SP.Velocity)
			$Chr(9)$SP.GetWalkingSpeed()
			$Chr(9)$SP.GroundSpeed
			$Chr(9)$int(SP.Physics)
			$Chr(9)$SP.bStasis
			$Chr(9)$string(SP.AnimSequence)
			$Chr(9)$SP.RandomWandering
			$Chr(9)$SP.restlessness
			$Chr(9)$SP.bInWorld
			$Chr(9)$SP.bHidden
			$Chr(9)$SP.LastRendered()
			$Chr(9)$SP.bInitialized
			$Chr(9)$SP.CollisionRadius
			$Chr(9)$SP.CollisionHeight
			$Chr(9)$SP.PrePivot.Z
			$Chr(9)$SP.DrawScale
			$Chr(9)$SP.bCollideWorld
			$Chr(9)$FloorDist
			$Chr(9)$SP.Default.CollisionHeight
			$Chr(9)$SP.PrePivotOffset.Z);
	}
}

// The exact iteration AISafeToShoot runs before a pawn decides to fire: what is
// between its eye and the player, in what order. That function takes the first
// entry as hitActor and stops at the first pawn or at Level, so both the order
// and whether Level appears at all decide the answer.
function SampleTraceActors( Pawn Player, ScriptedPawn Target )   // TA rows
{
	local Actor TraceActor;
	local Vector HitLocation, HitNormal, TraceStart, TraceEnd;
	local int Idx;

	TraceStart = Target.Location + vect(0,0,1) * Target.BaseEyeHeight;
	TraceEnd = Player.Location;

	foreach Target.TraceActors( Class'Actor', TraceActor, HitLocation, HitNormal, TraceEnd, TraceStart )
	{
		Emit("TA"$Chr(9)$Seq$Chr(9)$Stamp()
			$Chr(9)$TeleportTargetName
			$Chr(9)$Idx
			$Chr(9)$string(TraceActor.Name)
			$Chr(9)$string(TraceActor.Class)
			$Chr(9)$(TraceActor == Level)
			$Chr(9)$VSize(HitLocation - TraceStart)
			$Chr(9)$HitLocation.X$Chr(9)$HitLocation.Y$Chr(9)$HitLocation.Z
			$Chr(9)$HitNormal.X$Chr(9)$HitNormal.Y$Chr(9)$HitNormal.Z
			$Chr(9)$VSize(TraceEnd - TraceStart)
			$Chr(9)$TraceActor.Location.X$Chr(9)$TraceActor.Location.Y$Chr(9)$TraceActor.Location.Z
			$Chr(9)$TraceActor.CollisionRadius$Chr(9)$TraceActor.CollisionHeight
			$Chr(9)$TraceActor.bCollideActors$Chr(9)$TraceActor.bBlockActors
			$Chr(9)$TraceStart.X$Chr(9)$TraceStart.Y$Chr(9)$TraceStart.Z
			$Chr(9)$TraceEnd.X$Chr(9)$TraceEnd.Y$Chr(9)$TraceEnd.Z);
		Idx++;
		if ( Idx >= 12 )
			break;
	}

	if ( Idx == 0 )
		Emit("TA"$Chr(9)$Seq$Chr(9)$Stamp()$Chr(9)$TeleportTargetName$Chr(9)$"empty"
			$Chr(9)$VSize(TraceEnd - TraceStart));
}

// Which pawns are in a position to shoot the player, and what a single hit from
// each is worth. Damage that lands can then be attributed to a shooter instead
// of assumed to come from the one pawn the X and W rows follow.
function SampleArmedPawns( Pawn Player )   // AW rows
{
	local Pawn P;
	local DeusExWeapon W;

	for ( P = Level.PawnList; P != None; P = P.NextPawn )
	{
		W = DeusExWeapon(P.Weapon);
		if ( W == None || P == Player )
			continue;

		Emit("AW"$Chr(9)$Seq$Chr(9)$Stamp()
			$Chr(9)$string(P.Name)
			$Chr(9)$string(W.Class)
			$Chr(9)$W.HitDamage
			$Chr(9)$W.ClipCount
			$Chr(9)$W.AccurateRange
			$Chr(9)$W.MaxRange
			$Chr(9)$VSize(P.Location - Player.Location)
			$Chr(9)$string(P.Enemy)
			$Chr(9)$(P.Rotation.Yaw & 65535)
			$Chr(9)$(P.ViewRotation.Yaw & 65535)
			$Chr(9)$(Rotator(Player.Location - P.Location).Yaw & 65535)
			$Chr(9)$P.Rotation.Pitch
			$Chr(9)$P.ViewRotation.Pitch
			$Chr(9)$Rotator(Player.Location - P.Location).Pitch
			$Chr(9)$(P.DesiredRotation.Yaw & 65535)
			$Chr(9)$P.bRotateToDesired
			$Chr(9)$P.RotationRate.Yaw
			$Chr(9)$int(P.Physics)
			$Chr(9)$string(P.Focus)
			$Chr(9)$string(P.MoveTarget)
			$Chr(9)$P.Location.X$Chr(9)$P.Location.Y$Chr(9)$P.Location.Z
			$Chr(9)$Player.Location.X$Chr(9)$Player.Location.Y$Chr(9)$Player.Location.Z
			$Chr(9)$Player.CollisionRadius$Chr(9)$Player.CollisionHeight);
	}
}

// Every term DeusExPlayer.DXReduceDamage multiplies a ballistic hit by. An
// inactive augmentation has to report -1 so the guard there skips it; anything
// >= 0 is applied, so a wrong sentinel silently scales the damage instead.
function SampleDamageReduction( Pawn Player )   // DR rows
{
	local DeusExPlayer DXP;
	local int Reduced;

	DXP = DeusExPlayer(Player);
	if ( DXP == None )
		return;

	if ( DXP.AugmentationSystem == None || DXP.SkillSystem == None )
	{
		Emit("DR"$Chr(9)$Seq$Chr(9)$Stamp()$Chr(9)$"nosystem");
		return;
	}

	// What a 25 point rifle hit is actually worth after reduction. bCheckOnly
	// suppresses the screen flash, so this asks the question without answering it
	// on the player's behalf.
	DXP.DXReduceDamage( 25, 'Shot', DXP.Location, Reduced, True );

	Emit("DR"$Chr(9)$Seq$Chr(9)$Stamp()
		$Chr(9)$DXP.AugmentationSystem.GetAugLevelValue( class'AugBallistic' )
		$Chr(9)$DXP.AugmentationSystem.GetAugLevelValue( class'AugShield' )
		$Chr(9)$DXP.AugmentationSystem.GetAugLevelValue( class'AugEnviro' )
		$Chr(9)$DXP.AugmentationSystem.GetAugLevelValue( class'AugTarget' )
		$Chr(9)$DXP.SkillSystem.GetSkillLevelValue( class'SkillEnviro' )
		$Chr(9)$DXP.CombatDifficulty
		$Chr(9)$DXP.UsingChargedPickup( class'BallisticArmor' )
		$Chr(9)$DXP.Health
		$Chr(9)$DXP.HealthTorso
		$Chr(9)$DXP.HealthHead
		$Chr(9)$DXP.HealthArmLeft
		$Chr(9)$DXP.HealthArmRight
		$Chr(9)$DXP.HealthLegLeft
		$Chr(9)$DXP.HealthLegRight
		$Chr(9)$Reduced
		$Chr(9)$int(Level.NetMode));
}

function SampleProbeProps()
{
	local Pawn P, Target;
	local string S;
	local int i;

	for ( P = Level.PawnList; P != None; P = P.NextPawn )
		if ( string(P.Name) == TeleportTargetName )
			Target = P;

	if ( Target == None )
		return;

	for ( i = 0; i < 32; i++ )
	{
		if ( ProbeProps[i] == "" )
			continue;
		S = S$ProbeProps[i]$"="$Target.GetPropertyText(ProbeProps[i])$";";
	}

	Emit("D"$Chr(9)$Seq$Chr(9)$Stamp()$Chr(9)$TeleportTargetName$Chr(9)$S);
	Sink.Flush();
}

// One-shot: every ScriptedPawn's own Alliance name plus its InitialAlliances[8]
// editor array (raw exported text -- see IsA/GetPropertyText note up top).
function DumpInitialAlliances()
{
	local Pawn P;
	local int count;

	for ( P = Level.PawnList; P != None; P = P.NextPawn )
	{
		if ( !P.IsA('ScriptedPawn') )
			continue;

		count++;
		Emit("I"$Chr(9)$string(P.Name)
			$Chr(9)$string(P.Class)
			$Chr(9)$P.GetPropertyText("Alliance")
			$Chr(9)$P.GetPropertyText("bInitialized")
			$Chr(9)$P.GetPropertyText("InitialAlliances"));
	}
	Emit("#"$Chr(9)$"initial_dump_done"$Chr(9)$count);
	Sink.Flush();
}

// One-shot: the orders every ScriptedPawn starts with, and the actor they were
// pointed at. A pawn whose Orders arrive empty falls through FollowOrders() into
// Wandering, which looks like ordinary behaviour but is not the map's intent.
function DumpInitialOrders()
{
	local Pawn P;

	for ( P = Level.PawnList; P != None; P = P.NextPawn )
	{
		if ( !P.IsA('ScriptedPawn') )
			continue;

		Emit("O"$Chr(9)$string(P.Name)
			$Chr(9)$P.GetPropertyText("Orders")
			$Chr(9)$P.GetPropertyText("OrderTag")
			$Chr(9)$P.GetPropertyText("OrderActor")
			$Chr(9)$P.GetPropertyText("SeatActor")
			$Chr(9)$P.GetPropertyText("bSitAnywhere")
			$Chr(9)$P.GetPropertyText("InitialState")
			$Chr(9)$string(P.GetStateName()));
	}
	Sink.Flush();
}

// One-shot: every Seat in the level, with the fields ScriptedPawn.IsSeatValid()
// and FindBestSlot() consult before a pawn will sit in it.
function DumpSeats()
{
	local Actor A;

	foreach AllActors(class'Actor', A)
	{
		if ( !A.IsA('Seat') )
			continue;

		Emit("H"$Chr(9)$string(A.Name)
			$Chr(9)$string(A.Class)
			$Chr(9)$A.GetPropertyText("Location")
			$Chr(9)$A.GetPropertyText("InitialPosition")
			$Chr(9)$A.GetPropertyText("numSitPoints")
			$Chr(9)$A.GetPropertyText("sittingActor")
			$Chr(9)$A.GetPropertyText("bDeleteMe")
			$Chr(9)$string(A.Region.Zone.bWaterZone)
			$Chr(9)$string(A.Region.ZoneNumber));
	}
	Sink.Flush();
}

// One-shot diagnostic: for every pawn ordered to sit, the reachability answers
// FindBestSeat() depends on, per seat. Calling FindPathToward here perturbs the
// pawn's path state, so this is for a diagnostic run, not for regular captures.
function DumpSeatReach()
{
	local Pawn P;
	local Actor A;

	for ( P = Level.PawnList; P != None; P = P.NextPawn )
	{
		if ( !P.IsA('ScriptedPawn') || P.GetPropertyText("Orders") != "Sitting" )
			continue;

		foreach AllActors(class'Actor', A)
		{
			if ( !A.IsA('Seat') )
				continue;

			Emit("U"$Chr(9)$string(P.Name)
				$Chr(9)$string(A.Name)
				$Chr(9)$VSize(A.Location - P.Location)
				$Chr(9)$P.ActorReachable(A)
				$Chr(9)$string(P.FindPathToward(A))
				$Chr(9)$ScriptedPawn(P).IsSeatValid(A)
				$Chr(9)$VSize(A.Location - Seat(A).InitialPosition)
				$Chr(9)$Seat(A).numSitPoints
				$Chr(9)$string(Seat(A).sittingActor[0])
				$Chr(9)$A.Region.Zone.bWaterZone);
		}
	}
	Sink.Flush();
}

// Diagnostic: every tick of the run's opening seconds, what each sitting-ordered
// pawn's seat state machine is doing. Read-only, so unlike DumpSeatReach() it does
// not perturb the pawns it watches.
function SampleSeatTrace()
{
	local Pawn P;

	for ( P = Level.PawnList; P != None; P = P.NextPawn )
	{
		if ( !P.IsA('ScriptedPawn') || P.GetPropertyText("Orders") != "Sitting" )
			continue;

		Emit("M"$Chr(9)$Level.TimeSeconds
			$Chr(9)$string(P.Name)
			$Chr(9)$string(P.GetStateName())
			$Chr(9)$P.GetPropertyText("SeatActor")
			$Chr(9)$P.GetPropertyText("SeatSlot")
			$Chr(9)$P.GetPropertyText("bSeatLocationValid")
			$Chr(9)$P.GetPropertyText("bSitting")
			$Chr(9)$P.GetPropertyText("bUseFirstSeatOnly")
			$Chr(9)$P.GetPropertyText("bSeatHackUsed")
			$Chr(9)$P.GetPropertyText("SeatHack")
			$Chr(9)$P.GetPropertyText("OrderActor"));
	}
}

// Repeating: the live AlliancesEx table every ScriptedPawn actually consults
// at runtime (populated/mutated by ChangeAlly/AgitateAlliance as play proceeds).
function SampleLive()
{
	local Pawn P;

	Seq++;
	for ( P = Level.PawnList; P != None; P = P.NextPawn )
	{
		if ( !P.IsA('ScriptedPawn') || P.Health <= 0 )
			continue;

		Emit("E"$Chr(9)$Seq
			$Chr(9)$Stamp()
			$Chr(9)$string(P.Name)
			$Chr(9)$string(P.GetStateName())
			$Chr(9)$P.GetPropertyText("Enemy")
			$Chr(9)$P.GetPropertyText("AlliancesEx"));
	}
	Sink.Flush();
}

// Retail releases an enemy when its agitation toward that alliance decays back
// below 1.0 and IsValidEnemy stops holding, so the decay is what has to be
// compared, not the enemy pointer.
function SampleAgitation()
{
	local Pawn P;
	local ScriptedPawn SP;
	for ( P = Level.PawnList; P != None; P = P.NextPawn )
	{
		SP = ScriptedPawn(P);
		if ( SP == None || SP.Health <= 0 || SP.Enemy == None )
			continue;

		// ucc will not index AlliancesEx through a context expression (the array is
		// 256 bytes, over UE1's 255 limit), so sample the gate the release actually
		// consults instead of the table behind it.
		Emit("A"$Chr(9)$Seq
			$Chr(9)$Stamp()
			$Chr(9)$string(SP.Name)
			$Chr(9)$string(SP.Enemy.Name)
			$Chr(9)$SP.AgitationTimer
			$Chr(9)$SP.bAlliancesChanged
			$Chr(9)$SP.AgitationDecayRate
			$Chr(9)$SP.AgitationSustainTime
			$Chr(9)$int(SP.GetPawnAllianceType(SP.Enemy))
			$Chr(9)$SP.IsValidEnemy(SP.Enemy)
			$Chr(9)$string(SP.GetStateName())
			// Only ScriptedPawn.Tick touches these, one accumulating and one
			// draining, so they show whether that Tick body runs at all.
			// Pinned at 1.5 means IncreaseAgitation is firing every tick; decaying
			// to 0 means UpdateAgitation runs and nothing is re-agitating.
			$Chr(9)$SP.AgitationCheckTimer
			$Chr(9)$SP.WeaponTimer
			$Chr(9)$SP.AlarmTimer
			$Chr(9)$SP.ReloadTimer
			$Chr(9)$SP.FireTimer);
	}
	Sink.Flush();
}

// ScriptedPawn.Tick stops looking for non-player enemies once a pawn is both far
// from the player and off screen, so both halves of that gate have to agree
// across engines before the resulting enemy lists can be compared.
function SampleRenderGate()
{
	local Pawn P;
	local ScriptedPawn SP;
	local bool bCheckOther;

	for ( P = Level.PawnList; P != None; P = P.NextPawn )
	{
		SP = ScriptedPawn(P);
		if ( SP == None || SP.Health <= 0 )
			continue;

		bCheckOther = true;
		if ( SP.bTickVisibleOnly && (SP.DistanceFromPlayer > 600) && (SP.LastRendered() >= 5.0) )
			bCheckOther = false;

		Emit("X"$Chr(9)$Seq
			$Chr(9)$Stamp()
			$Chr(9)$string(SP.Name)
			$Chr(9)$SP.DistanceFromPlayer
			$Chr(9)$SP.LastRendered()
			$Chr(9)$SP.bTickVisibleOnly
			$Chr(9)$bCheckOther
			$Chr(9)$SP.bHidden
			$Chr(9)$string(SP.Enemy.Name)
			// Sampled where each pawn stands, so both engines report light at the
			// same world points for as long as the pawns have not drifted apart.
			$Chr(9)$SP.AIGetLightLevel(SP.Location)
			$Chr(9)$SP.Location.X
			$Chr(9)$SP.Location.Y
			$Chr(9)$SP.Location.Z
			// What drives a pawn into and out of state Seeking: EnemyReadiness rises
			// past SightPercentage on a partial sighting, and SeekLevel is the budget
			// the state spends down before it gives up.
			$Chr(9)$SP.EnemyReadiness
			$Chr(9)$SP.ReactionLevel
			$Chr(9)$SP.SeekLevel
			$Chr(9)$SP.SightPercentage
			$Chr(9)$SP.bSeekLocation
			$Chr(9)$SP.SeekType);
	}
	Sink.Flush();
}

// Emits the raw perception terms between one named pawn and another, whatever
// either of them currently thinks of the other.
function SampleProbePair()
{
	local Pawn P, A, B;

	if ( ProbePawnA == "" || ProbePawnB == "" )
		return;

	for ( P = Level.PawnList; P != None; P = P.NextPawn )
	{
		if ( string(P.Name) == ProbePawnA )
			A = P;
		else if ( string(P.Name) == ProbePawnB )
			B = P;
	}
	if ( A == None || B == None || ScriptedPawn(A) == None )
		return;

	Emit("J"$Chr(9)$Seq
		$Chr(9)$Stamp()
		$Chr(9)$ProbePawnA
		$Chr(9)$ProbePawnB
		$Chr(9)$VSize(A.Location - B.Location)
		$Chr(9)$A.AICanSee(B, ScriptedPawn(A).ComputeActorVisibility(B), true, true, true, true)
		$Chr(9)$ScriptedPawn(A).ComputeActorVisibility(B)
		$Chr(9)$A.AIGetLightLevel(B.Location)
		$Chr(9)$ScriptedPawn(A).IsValidEnemy(B)
		$Chr(9)$int(ScriptedPawn(A).GetPawnAllianceType(B))
		$Chr(9)$string(A.GetStateName())
		$Chr(9)$A.VisibilityThreshold
		$Chr(9)$A.MinAngularSize
		$Chr(9)$A.AngularResolution
		$Chr(9)$B.CollisionRadius
		$Chr(9)$B.CollisionHeight
		$Chr(9)$A.AIGetLightLevel(A.Location));
	Sink.Flush();
}

// Separates "was the global Tick reached" from "what argument did it get".
function SampleGlobalTick()
{
	if ( TickProbe == None )
		return;

	Emit("B"$Chr(9)$Seq
		$Chr(9)$Stamp()
		$Chr(9)$TickProbe.StateCalls
		$Chr(9)$TickProbe.GlobalCalls
		$Chr(9)$TickProbe.StateAccum
		$Chr(9)$TickProbe.GlobalAccum
		$Chr(9)$TickProbe.LastStateDelta
		$Chr(9)$TickProbe.LastGlobalDelta
		$Chr(9)$Level.TimeSeconds);

	if ( TickProbeChild != None )
		Emit("Y"$Chr(9)$Seq
			$Chr(9)$Stamp()
			$Chr(9)$TickProbeChild.StateCalls
			$Chr(9)$TickProbeChild.GlobalCalls
			$Chr(9)$TickProbeChild.StateAccum
			$Chr(9)$TickProbeChild.GlobalAccum
			$Chr(9)$TickProbeChild.LastStateDelta
			$Chr(9)$TickProbeChild.LastGlobalDelta
			$Chr(9)$Level.TimeSeconds);

	if ( SuperProbe != None )
		Emit("Z"$Chr(9)$Seq
			$Chr(9)$Stamp()
			$Chr(9)$SuperProbe.StateCalls
			$Chr(9)$SuperProbe.GlobalCalls
			$Chr(9)$SuperProbe.AfterSuperCalls
			$Chr(9)$SuperProbe.GlobalAccum
			$Chr(9)$SuperProbe.AfterSuperAccum
			$Chr(9)$Level.TimeSeconds);

	if ( OrderProbe != None )
		Emit("TO"$Chr(9)$Seq
			$Chr(9)$Stamp()
			$Chr(9)$OrderProbe.Frames
			$Chr(9)$OrderProbe.TickFirst
			$Chr(9)$OrderProbe.CodeFirst);

	if ( PhysProbe != None )
		Emit("PO"$Chr(9)$Seq
			$Chr(9)$Stamp()
			$Chr(9)$PhysProbe.Frames
			$Chr(9)$PhysProbe.MovedBetween
			$Chr(9)$PhysProbe.Traveled
			$Chr(9)$PhysProbe.SumDT
			$Chr(9)$PhysProbe.DriftSpeed
			$Chr(9)$PhysProbe.Lag
			$Chr(9)$PhysProbe.LagMin
			$Chr(9)$PhysProbe.LagMax
			$Chr(9)$PhysProbe.Physics);
	Sink.Flush();
}

//----------------------------------------------------------------- formatting

final function Emit( string S )
{
	if ( Sink != None )
		Sink.Line(S);
}

final function string Stamp()
{
	return string(int(Level.TimeSeconds * 1000));
}

defaultproperties
{
	InitialDumpDelay=1.500000
	SampleHz=1.000000
	AutoQuitDelay=120.000000
	MinTeleportDelay=3.000000
	bKeepPlayerAtTarget=True
	ApproachDistance=120.000000
	bTeleportPlayerToTarget=True
	TeleportTargetName="PaulDenton0"
	PaulApproachOffset=(X=96.000000,Y=0.000000,Z=0.000000)
	bAlwaysTick=True
	ProbeProps(0)="bReactPresence"
	ProbeProps(1)="bLookingForEnemy"
	ProbeProps(2)="bNoNegativeAlliances"
	ProbeProps(3)="EnemyReadiness"
	ProbeProps(4)="ReactionLevel"
	ProbeProps(5)="SightPercentage"
	ProbeProps(6)="CycleIndex"
	ProbeProps(7)="CycleTimer"
	ProbeProps(8)="EnemyLastSeen"
	ProbeProps(9)="EnemyTimeout"
	// Read through GetPropertyText rather than as direct field accesses: a direct
	// Target.EnemyTimer read spins the SurrealEngine VM forever.
	ProbeProps(16)="EnemyTimer"
	ProbeProps(17)="bMustFaceTarget"
	ProbeProps(18)="FireAngle"
	// ComputeBestFiringPosition inputs: DEST_SameLocation needs acrossDist==0 &&
	// awayDist==dist && !bUseProjVector && !bUseSprint, so these decide whether
	// the pawn keeps looping ContinueFire or detours through RunToRange.
	ProbeProps(19)="bSprint"
	ProbeProps(20)="bCanStrafe"
	ProbeProps(21)="SprintRate"
	ProbeProps(22)="CloseCombatMult"
	ProbeProps(23)="bAvoidHarm"
	ProbeProps(24)="HarmAccuracy"
	ProbeProps(25)="AvoidAccuracy"
	ProbeProps(26)="GroundSpeed"
	// Combat MoveTo asks for MaxDesiredSpeed while patrol asks for
	// GetWalkingSpeed(); the engine then walks at GroundSpeed*DesiredSpeed.
	ProbeProps(27)="DesiredSpeed"
	ProbeProps(28)="MaxDesiredSpeed"
	ProbeProps(29)="WalkingSpeed"
	ProbeProps(30)="AccelRate"
	ProbeProps(31)="bReducedSpeed"
	ProbeProps(10)="Orders"
	ProbeProps(11)="OrderTag"
	ProbeProps(12)="Alliance"
	ProbeProps(13)="bHateShot"
	ProbeProps(14)="bFearShot"
	ProbeProps(15)="bAvoidAim"
}
