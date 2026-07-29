//=============================================================================
// Emits per-sample pawn state plus damage, death and frag events so retail
// bot behaviour can be measured against a reimplementation's telemetry.
//
//   ?Mutator=BotTelemetry.BotTelemetryMutator
//
// Output lands in ../Logs/bottelemetry.log as tab-separated records:
//   #  header / footer / final scores
//   S  state sample
//   H  damage taken
//   D  death
//   K  frag credited
//=============================================================================
class BotTelemetryMutator extends Mutator config(BotTelemetry);

var config float SampleHz;
var config bool  bLogDamage;
var config bool  bBotsOnly;

// Native probes. Both engines run this same script, so anything that differs
// between them is native behaviour, which is what these record.
var config bool  bProbeWaterJump;    // log CheckWaterJump's internals live
var config bool  bProbeTraceCorpus;  // dump a deterministic trace corpus at start
var config bool  bProbeReachCorpus;  // dump actorReachable answers for navpoint pairs
var config bool  bProbeNodeGraph;    // dump the reachspec index arrays per navpoint
var config bool  bProbeVisionCorpus; // dump CanSee/LineOfSightTo answers over a yaw sweep
var config float ProbeReachDist;     // max pair separation for the reach corpus
var config float ProbeVisionDist;    // max observer-target separation
var config int   ProbeVisionYawSteps;// yaw samples per pair
var config int   ProbeVisionStride;  // use every Nth navigation point as an observer
var config int   ProbeVisionRotMode; // 0 body rotation, 1 view rotation, 2 both
var config bool  bExitAfterProbes;   // finalise the log and quit once the dumps are done
var config float ProbeRadius;        // collision extent used by the corpus
var config float ProbeHeight;
var config float ProbeDist;

var BotTelemetryLog Sink;
var float Accum;
var float Period;
var int   Seq;

function PostBeginPlay()
{
	Super.PostBeginPlay();

	if ( SampleHz <= 0 )
		SampleHz = 30;
	Period = 1.0 / SampleHz;

	Sink = Spawn(class'BotTelemetryLog');
	Sink.StartLog();

	Emit("#"$Chr(9)$"bottelemetry"$Chr(9)$"v1"
		$Chr(9)$"map="$string(Level.Outer.Name)
		$Chr(9)$"title="$Level.Title
		$Chr(9)$"game="$string(Level.Game.Class)
		$Chr(9)$"hz="$SampleHz);
	Emit("#"$Chr(9)$"cols_S"$Chr(9)$"seq ms name physics health x y z vx vy vz zone zoneflags footflags headflags movetarget route0 route1 base state enemy orders");
	Emit("#"$Chr(9)$"cols_H"$Chr(9)$"seq ms victim damage type instigator health_before footflags zoneflags headflags");
	Emit("#"$Chr(9)$"cols_D"$Chr(9)$"seq ms victim killer type x y z zone");
	Emit("#"$Chr(9)$"cols_W"$Chr(9)$"seq ms name yaw x y z radius height maxstep t1_hit t1_class t1_nx1000 t1_ny1000 t1_nz1000 t2_hit t2_class verdict physics zone");
	Emit("#"$Chr(9)$"cols_T"$Chr(9)$"idx node dir kind sx sy sz ex ey ez hit hitclass hx hy hz nx1000 ny1000 nz1000");
	Emit("#"$Chr(9)$"cols_R"$Chr(9)$"idx from to dist dz actorreachable pointreachable");
	Emit("#"$Chr(9)$"cols_G"$Chr(9)$"idx name class x y z extracost endpoint playeronly paths upstream pruned visnoreach");
	Emit("#"$Chr(9)$"cols_V"$Chr(9)$"idx obs tgt dist dz yaw cos1000 periph1000 sightradius visibility cansee los");

	Level.Game.RegisterDamageMutator(Self);
	if ( bProbeTraceCorpus )
		DumpTraceCorpus();
	if ( bProbeNodeGraph )
		DumpNodeGraph();
	if ( bProbeReachCorpus )
		DumpReachCorpus();
	if ( bProbeVisionCorpus )
		DumpVisionCorpus();
	Sink.Flush();

	// The probe dumps do not need a match to run. Finalising here means the log is
	// complete the moment they are, instead of waiting out a time limit.
	if ( bExitAfterProbes )
	{
		Emit("#"$Chr(9)$"probes_only_end"$Chr(9)$Stamp());
		Sink.StopLog();
		ConsoleCommand("EXIT");
	}
}

// A deterministic set of world traces anchored to the map's NavigationPoints, run
// with and without a collision extent. Both engines load the same map, so this
// corpus is directly replayable and diffable. bTraceActors is false so only BSP
// and movers participate, which isolates the box-versus-BSP question.
function DumpTraceCorpus()
{
	local NavigationPoint N;
	local int idx, d;
	local vector dir, st, en, ext, hl, hn;
	local rotator r;
	local actor hit;

	ext = ProbeRadius * vect(1,1,0);
	ext.Z = ProbeHeight;

	for ( N = Level.NavigationPointList; N != None; N = N.nextNavigationPoint )
	{
		for ( d = 0; d < 4; d++ )
		{
			r.Yaw = d * 16384;
			dir = vector(r);
			st = N.Location;
			en = st + ProbeDist * dir;

			hit = Trace(hl, hn, en, st, false, ext);
			EmitTrace(idx, N, d, "box", st, en, hit, hl, hn);
			idx++;

			hit = Trace(hl, hn, en, st, false);
			EmitTrace(idx, N, d, "zero", st, en, hit, hl, hn);
			idx++;

			// CheckWaterJump traces with bTraceActors true, so cover that too.
			hit = Trace(hl, hn, en, st, true, ext);
			EmitTrace(idx, N, d, "boxact", st, en, hit, hl, hn);
			idx++;

			hit = Trace(hl, hn, en, st, true);
			EmitTrace(idx, N, d, "zeroact", st, en, hit, hl, hn);
			idx++;
		}
	}
	Emit("#"$Chr(9)$"corpus_done"$Chr(9)$idx);
	Sink.Flush();
}

// Asks actorReachable()/pointReachable() for every ordered pair of navigation
// points within ProbeReachDist, from a bot sized probe standing on the source
// point. actorReachable applies a 1000 unit gate to non-pawn targets, so pairs
// beyond that are false by definition and not worth logging.
function DumpReachCorpus()
{
	local NavigationPoint A, B;
	local Pawn S;
	local int idx, ar, pr;
	local float d;

	if ( Level.NavigationPointList == None )
	{
		Emit("#"$Chr(9)$"reach_corpus_failed"$Chr(9)$"no_navpoints");
		return;
	}

	// TMale1 is the pawn class the bots themselves use, so its collision size,
	// step height and movement flags are exactly what a bot would ask with.
	S = Spawn(class'Botpack.TMale1',,, Level.NavigationPointList.Location);
	if ( S == None )
	{
		Emit("#"$Chr(9)$"reach_corpus_failed"$Chr(9)$"probe_spawn");
		return;
	}
	S.SetCollision(False, False, False);
	S.bHidden = True;
	Emit("#"$Chr(9)$"reach_probe"$Chr(9)$string(S.Class)
		$Chr(9)$"r="$int(S.CollisionRadius)
		$Chr(9)$"h="$int(S.CollisionHeight)
		$Chr(9)$"step="$int(S.MaxStepHeight)
		$Chr(9)$"player="$string(S.bIsPlayer)
		$Chr(9)$"walk="$string(S.bCanWalk)
		$Chr(9)$"swim="$string(S.bCanSwim)
		$Chr(9)$"fly="$string(S.bCanFly));

	for ( A = Level.NavigationPointList; A != None; A = A.nextNavigationPoint )
	{
		if ( !S.SetLocation(A.Location) )
		{
			Emit("#"$Chr(9)$"reach_skip"$Chr(9)$string(A.Name)$Chr(9)$"setlocation");
			continue;
		}
		S.SetPhysics(PHYS_Walking);

		for ( B = Level.NavigationPointList; B != None; B = B.nextNavigationPoint )
		{
			if ( B == A )
				continue;
			d = VSize(B.Location - A.Location);
			if ( d > ProbeReachDist )
				continue;

			ar = 0;
			pr = 0;
			if ( S.ActorReachable(B) )
				ar = 1;
			if ( S.PointReachable(B.Location) )
				pr = 1;

			Emit("R"$Chr(9)$idx
				$Chr(9)$string(A.Name)
				$Chr(9)$string(B.Name)
				$Chr(9)$int(d)
				$Chr(9)$int(B.Location.Z - A.Location.Z)
				$Chr(9)$ar
				$Chr(9)$pr);
			idx++;
		}
		Sink.Flush();
	}

	S.Destroy();
	Emit("#"$Chr(9)$"reach_corpus_done"$Chr(9)$idx);
	Sink.Flush();
}

// Asks CanSee()/LineOfSightTo() for every ordered pair of navigation points
// within ProbeVisionDist, from every ProbeVisionStride'th point as observer,
// swept over three PeripheralVision settings and a full yaw turn. Observer
// and target are both hidden-collision TMale1s; only the observer is hidden,
// so the target's own bHidden/visibility state doesn't confound the answer.
function DumpVisionCorpus()
{
	local NavigationPoint A, B;
	local Pawn Observer, Target;
	local int idx, stride, count, i, yi;
	local float d;
	local float Periph[3];
	local rotator r;
	local int cs, los;
	local vector fwd;

	if ( Level.NavigationPointList == None )
	{
		Emit("#"$Chr(9)$"vision_corpus_failed"$Chr(9)$"no_navpoints");
		return;
	}

	Observer = Spawn(class'Botpack.TMale1',,, Level.NavigationPointList.Location);
	Target = Spawn(class'Botpack.TMale1',,, Level.NavigationPointList.Location);
	if ( Observer == None || Target == None )
	{
		Emit("#"$Chr(9)$"vision_corpus_failed"$Chr(9)$"probe_spawn");
		return;
	}
	Observer.SetCollision(False, False, False);
	Target.SetCollision(False, False, False);
	Observer.bHidden = True;

	Emit("#"$Chr(9)$"vision_probe"$Chr(9)$"TMale1"
		$Chr(9)$"r="$int(Observer.CollisionRadius)
		$Chr(9)$"h="$int(Observer.CollisionHeight)
		$Chr(9)$"sightradius="$int(Observer.SightRadius)
		$Chr(9)$"periphdefault="$int(1000 * Observer.PeripheralVision));

	Periph[0] = 0.700000;
	Periph[1] = 0.000000;
	Periph[2] = -0.200000;

	stride = ProbeVisionStride;
	if ( stride <= 0 )
		stride = 1;

	for ( A = Level.NavigationPointList; A != None; A = A.nextNavigationPoint )
	{
		if ( (count % stride) != 0 )
		{
			count++;
			continue;
		}
		count++;

		if ( !Observer.SetLocation(A.Location) )
		{
			Emit("#"$Chr(9)$"vision_skip"$Chr(9)$string(A.Name)$Chr(9)$"setlocation");
			continue;
		}

		for ( B = Level.NavigationPointList; B != None; B = B.nextNavigationPoint )
		{
			if ( B == A )
				continue;
			d = VSize(B.Location - A.Location);
			if ( d > ProbeVisionDist )
				continue;

			if ( !Target.SetLocation(B.Location) )
			{
				Emit("#"$Chr(9)$"vision_skip"$Chr(9)$string(B.Name)$Chr(9)$"setlocation");
				continue;
			}

			for ( i = 0; i < 3; i++ )
			{
				Observer.PeripheralVision = Periph[i];

				for ( yi = 0; yi < ProbeVisionYawSteps; yi++ )
				{
					r.Pitch = 0;
					r.Roll = 0;
					r.Yaw = yi * (65536 / ProbeVisionYawSteps);
					// Which rotation the sweep turns decides which one the cosine is
					// measured against, so a mode that turns the wrong one shows up
					// as an answer that ignores the sweep entirely.
					if ( ProbeVisionRotMode != 1 )
						Observer.SetRotation(r);
					if ( ProbeVisionRotMode != 0 )
						Observer.ViewRotation = r;
					if ( ProbeVisionRotMode == 1 )
						fwd = vector(Observer.ViewRotation);
					else
						fwd = vector(Observer.Rotation);

					cs = 0;
					los = 0;
					if ( Observer.CanSee(Target) )
						cs = 1;
					if ( Observer.LineOfSightTo(Target) )
						los = 1;

					Emit("V"$Chr(9)$idx
						$Chr(9)$string(A.Name)
						$Chr(9)$string(B.Name)
						$Chr(9)$int(d)
						$Chr(9)$int(B.Location.Z - A.Location.Z)
						$Chr(9)$r.Yaw
						$Chr(9)$int(1000 * (Normal(Target.Location - Observer.Location) dot fwd))
						$Chr(9)$int(1000 * Observer.PeripheralVision)
						$Chr(9)$int(Observer.SightRadius)
						$Chr(9)$int(Target.Visibility)
						$Chr(9)$cs
						$Chr(9)$los);
					idx++;
				}
			}
		}
		Sink.Flush();
	}

	Observer.Destroy();
	Target.Destroy();
	Emit("#"$Chr(9)$"vision_corpus_done"$Chr(9)$idx);
	Sink.Flush();
}

// Dumps the navigation graph exactly as the engine holds it: the reachspec index
// arrays on every NavigationPoint. Both engines load the same .unr, so these
// indices are directly comparable and show whether the graph itself differs.
function DumpNodeGraph()
{
	local NavigationPoint N;
	local int i, count;
	local string paths, up, pruned, vis;

	for ( N = Level.NavigationPointList; N != None; N = N.nextNavigationPoint )
	{
		paths = "";
		up = "";
		pruned = "";
		vis = "";
		for ( i = 0; i < 16; i++ )
		{
			if ( i > 0 )
			{
				paths = paths$",";
				up = up$",";
				pruned = pruned$",";
				vis = vis$",";
			}
			paths = paths$N.Paths[i];
			up = up$N.upstreamPaths[i];
			pruned = pruned$N.PrunedPaths[i];
			if ( N.VisNoReachPaths[i] != None )
				vis = vis$string(N.VisNoReachPaths[i].Name);
			else
				vis = vis$"-";
		}
		Emit("G"$Chr(9)$count
			$Chr(9)$string(N.Name)
			$Chr(9)$string(N.Class)
			$Chr(9)$int(N.Location.X)$Chr(9)$int(N.Location.Y)$Chr(9)$int(N.Location.Z)
			$Chr(9)$N.ExtraCost
			$Chr(9)$string(N.bEndPoint)
			$Chr(9)$string(N.bPlayerOnly)
			$Chr(9)$paths
			$Chr(9)$up
			$Chr(9)$pruned
			$Chr(9)$vis);
		count++;
	}
	Emit("#"$Chr(9)$"nodegraph_done"$Chr(9)$count);
	Sink.Flush();
}

final function EmitTrace( int idx, NavigationPoint N, int d, string kind,
	vector st, vector en, actor hit, vector hl, vector hn )
{
	Emit("T"$Chr(9)$idx
		$Chr(9)$string(N.Name)
		$Chr(9)$d
		$Chr(9)$kind
		$Chr(9)$int(st.X)$Chr(9)$int(st.Y)$Chr(9)$int(st.Z)
		$Chr(9)$int(en.X)$Chr(9)$int(en.Y)$Chr(9)$int(en.Z)
		$Chr(9)$ActorName(hit)
		$Chr(9)$ClassName(hit)
		$Chr(9)$int(hl.X)$Chr(9)$int(hl.Y)$Chr(9)$int(hl.Z)
		$Chr(9)$int(hn.X*1000)$Chr(9)$int(hn.Y*1000)$Chr(9)$int(hn.Z*1000));
}

// Replicates CheckWaterJump step by step and records each native result, then
// calls the real thing so the replication can be checked against it.
function ProbeWaterJump( Pawn P )
{
	local vector checkpoint, start, checkNorm, ext, hl, hn, wallN;
	local actor hit1, hit2;
	local vector n1;
	local bool verdict;

	checkpoint = vector(P.Rotation);
	checkpoint.Z = 0.0;
	checkNorm = Normal(checkpoint);
	checkpoint = P.Location + P.CollisionRadius * checkNorm;
	ext = P.CollisionRadius * vect(1,1,0);
	ext.Z = P.CollisionHeight;

	hit1 = P.Trace(hl, hn, checkpoint, P.Location, true, ext);
	n1 = hn;

	start = P.Location;
	start.Z += 1.1 * P.MaxStepHeight;
	checkpoint = start + 2 * P.CollisionRadius * checkNorm;
	hit2 = P.Trace(hl, hn, checkpoint, start, true);

	verdict = P.CheckWaterJump(wallN);

	Emit("W"$Chr(9)$Seq
		$Chr(9)$Stamp()
		$Chr(9)$PawnName(P)
		$Chr(9)$P.Rotation.Yaw
		$Chr(9)$int(P.Location.X)$Chr(9)$int(P.Location.Y)$Chr(9)$int(P.Location.Z)
		$Chr(9)$int(P.CollisionRadius)$Chr(9)$int(P.CollisionHeight)$Chr(9)$int(P.MaxStepHeight)
		$Chr(9)$ActorName(hit1)$Chr(9)$ClassName(hit1)
		$Chr(9)$int(n1.X*1000)$Chr(9)$int(n1.Y*1000)$Chr(9)$int(n1.Z*1000)
		$Chr(9)$ActorName(hit2)$Chr(9)$ClassName(hit2)
		$Chr(9)$verdict
		$Chr(9)$PhysName(P.Physics)
		$Chr(9)$ZoneName(P.Region));
}

function Tick( float DeltaTime )
{
	Accum += DeltaTime;
	if ( Accum < Period )
		return;
	Accum = 0;
	Sample();
}

function Sample()
{
	local Pawn P;

	Seq++;
	for ( P = Level.PawnList; P != None; P = P.NextPawn )
	{
		if ( P.Health > 0 && (!bBotsOnly || P.bIsPlayer) )
			Emit("S"$Chr(9)$Seq
				$Chr(9)$Stamp()
				$Chr(9)$PawnName(P)
				$Chr(9)$PhysName(P.Physics)
				$Chr(9)$P.Health
				$Chr(9)$int(P.Location.X)$Chr(9)$int(P.Location.Y)$Chr(9)$int(P.Location.Z)
				$Chr(9)$int(P.Velocity.X)$Chr(9)$int(P.Velocity.Y)$Chr(9)$int(P.Velocity.Z)
				$Chr(9)$ZoneName(P.Region)
				$Chr(9)$ZoneFlags(P.Region)
				$Chr(9)$ZoneFlags(P.FootRegion)
				$Chr(9)$ZoneFlags(P.HeadRegion)
				$Chr(9)$ActorName(P.MoveTarget)
				$Chr(9)$RouteName(P, 0)
				$Chr(9)$RouteName(P, 1)
				$Chr(9)$ActorName(P.Base)
				$Chr(9)$string(P.GetStateName())
				$Chr(9)$ActorName(P.Enemy)
				$Chr(9)$Orders(P));

		// Probe wherever a water jump could plausibly be attempted.
		if ( bProbeWaterJump && P.Health > 0
			&& (P.Physics == PHYS_Swimming || P.Region.Zone.bWaterZone
				|| P.FootRegion.Zone.bWaterZone || P.FootRegion.Zone.bPainZone) )
			ProbeWaterJump(P);
	}
	Sink.Flush();
}

function MutatorTakeDamage( out int ActualDamage, Pawn Victim, Pawn InstigatedBy,
	out Vector HitLocation, out Vector Momentum, name DamageType )
{
	if ( bLogDamage && Victim != None )
	{
		// Victim.Health is still the pre-damage value here.
		Emit("H"$Chr(9)$Seq
			$Chr(9)$Stamp()
			$Chr(9)$PawnName(Victim)
			$Chr(9)$ActualDamage
			$Chr(9)$string(DamageType)
			$Chr(9)$InstigatorName(InstigatedBy)
			$Chr(9)$Victim.Health
			$Chr(9)$ZoneFlags(Victim.FootRegion)
			$Chr(9)$ZoneFlags(Victim.Region)
			$Chr(9)$ZoneFlags(Victim.HeadRegion));
		Sink.Flush();
	}
	Super.MutatorTakeDamage(ActualDamage, Victim, InstigatedBy, HitLocation, Momentum, DamageType);
}

function bool PreventDeath( Pawn Killed, Pawn Killer, name DamageType, vector HitLocation )
{
	if ( Killed != None )
	{
		Emit("D"$Chr(9)$Seq
			$Chr(9)$Stamp()
			$Chr(9)$PawnName(Killed)
			$Chr(9)$InstigatorName(Killer)
			$Chr(9)$string(DamageType)
			$Chr(9)$int(Killed.Location.X)$Chr(9)$int(Killed.Location.Y)$Chr(9)$int(Killed.Location.Z)
			$Chr(9)$ZoneName(Killed.Region));
		Sink.Flush();
	}
	return Super.PreventDeath(Killed, Killer, DamageType, HitLocation);
}

function ScoreKill( Pawn Killer, Pawn Other )
{
	Emit("K"$Chr(9)$Seq$Chr(9)$Stamp()$Chr(9)$InstigatorName(Killer)$Chr(9)$InstigatorName(Other));
	Sink.Flush();
	Super.ScoreKill(Killer, Other);
}

function bool HandleEndGame()
{
	DumpScores();
	return Super.HandleEndGame();
}

function DumpScores()
{
	local Pawn P;

	Emit("#"$Chr(9)$"scores"$Chr(9)$"name frags deaths bot");
	for ( P = Level.PawnList; P != None; P = P.NextPawn )
		if ( P.PlayerReplicationInfo != None )
			Emit("#"$Chr(9)$"score"
				$Chr(9)$PawnName(P)
				$Chr(9)$int(P.PlayerReplicationInfo.Score)
				$Chr(9)$P.PlayerReplicationInfo.Deaths
				$Chr(9)$P.PlayerReplicationInfo.bIsABot);
	Emit("#"$Chr(9)$"end"$Chr(9)$Stamp());
	Sink.StopLog();
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

final function string ActorName( Actor A )
{
	if ( A == None )
		return "-";
	return string(A.Name);
}

final function string PawnName( Pawn P )
{
	if ( P == None )
		return "-";
	if ( P.PlayerReplicationInfo != None && P.PlayerReplicationInfo.PlayerName != "" )
		return P.PlayerReplicationInfo.PlayerName;
	return string(P.Name);
}

final function string InstigatorName( Pawn P )
{
	return PawnName(P);
}

// Distinguishes a BSP/world hit (no actor class) from an actor hit.
final function string ClassName( Actor A )
{
	if ( A == None )
		return "-";
	return string(A.Class);
}

final function string ZoneName( PointRegion R )
{
	if ( R.Zone == None )
		return "-";
	return string(R.Zone.Name);
}

// Compact zone descriptor: pain/water flags plus the zone's damage rate.
final function string ZoneFlags( PointRegion R )
{
	local string S;

	if ( R.Zone == None )
		return "-";
	if ( R.Zone.bPainZone )
		S = S$"P";
	if ( R.Zone.bWaterZone )
		S = S$"W";
	if ( S == "" )
		S = ".";
	return S$"/"$R.Zone.DamagePerSec;
}

final function string RouteName( Pawn P, int i )
{
	if ( P.RouteCache[i] == None )
		return "-";
	return string(P.RouteCache[i].Name);
}

final function string Orders( Pawn P )
{
	if ( Bot(P) == None )
		return "-";
	return string(Bot(P).Orders)$"/"$ActorName(Bot(P).OrderObject);
}

final function string PhysName( EPhysics Ph )
{
	switch ( Ph )
	{
		case PHYS_None:          return "None";
		case PHYS_Walking:       return "Walking";
		case PHYS_Falling:       return "Falling";
		case PHYS_Swimming:      return "Swimming";
		case PHYS_Flying:        return "Flying";
		case PHYS_Rotating:      return "Rotating";
		case PHYS_Projectile:    return "Projectile";
		case PHYS_Rolling:       return "Rolling";
		case PHYS_Interpolating: return "Interpolating";
		case PHYS_MovingBrush:   return "MovingBrush";
		case PHYS_Spider:        return "Spider";
		case PHYS_Trailer:       return "Trailer";
	}
	return string(int(Ph));
}

defaultproperties
{
	SampleHz=30.000000
	bLogDamage=True
	bBotsOnly=False
	bProbeWaterJump=True
	bProbeTraceCorpus=True
	bProbeReachCorpus=True
	bProbeNodeGraph=True
	ProbeReachDist=1000.000000
	ProbeVisionDist=800.000000
	ProbeVisionYawSteps=32
	ProbeVisionStride=16
	ProbeVisionRotMode=0
	ProbeRadius=17.000000
	ProbeHeight=39.000000
	ProbeDist=60.000000
	bAlwaysTick=True
}
