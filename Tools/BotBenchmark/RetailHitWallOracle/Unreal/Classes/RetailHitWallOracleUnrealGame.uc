// Unattended Unreal Gold 226b runner for controlled walking contacts.
class RetailHitWallOracleUnrealGame extends UnrealShare.DeathMatchGame
    config(RetailHitWallOracleUnreal);

var config int OracleMinHitWallMilli;
var config int OracleCase;
var config int OracleDurationSeconds;
var bool bOracleStarted;
var bool bOracleSetupAttempted;
var bool bOracleCompleted;
var RetailHitWallOracleUnrealBot OracleProbe;
var string OracleRunId;
var int OracleSequence;

function int NextOracleSequence()
{
    OracleSequence++;
    return OracleSequence;
}

function MarkOracleComplete()
{
    bOracleCompleted = True;
}

event InitGame(string Options, out string Error)
{
    // Unreal Gold's globalconfig defaults can override the package default
    // before GameInfo creates LocalLog, so set these before Super.InitGame.
    bLocalLog = True;
    bWorldLog = False;
    Super.InitGame(Options, Error);
    OracleMinHitWallMilli = GetIntOption(Options, "OracleMinHitWallMilli", OracleMinHitWallMilli);
    OracleCase = GetIntOption(Options, "OracleCase", OracleCase);
    OracleDurationSeconds = GetIntOption(Options, "OracleDurationSeconds", OracleDurationSeconds);
    OracleRunId = ParseOption(Options, "OracleRunId");
    if (OracleRunId == "")
        OracleRunId = "missing-run-id";
}

function LogOracle(string EventName, string Detail)
{
    local string Record;

    Record = "0.00" $ Chr(9) $ "minhitwall_oracle" $ Chr(9) $ EventName $ Chr(9)
        $ "run=" $ OracleRunId $ ";seq=" $ NextOracleSequence() $ ";" $ Detail;
    if (LocalLog != None)
        LocalLog.LogEventString(Record);
    Log(Record);
}

function FlushOracleFallback()
{
    local int Index;

    // UCC 226b buffers its ordinary server log. This bounded filler forces
    // the preceding tagged records to durable storage before runner shutdown.
    for (Index = 0; Index < 24; Index++)
        Log("minhitwall_oracle_flush");
}

function vector CandidateDirection(int Candidate)
{
    if (Candidate == 0) return vect(1,0,0);
    if (Candidate == 1) return vect(0,1,0);
    if (Candidate == 2) return vect(-1,0,0);
    if (Candidate == 3) return vect(0,-1,0);
    if (Candidate == 4) return Normal(vect(1,1,0));
    if (Candidate == 5) return Normal(vect(-1,1,0));
    if (Candidate == 6) return Normal(vect(-1,-1,0));
    return Normal(vect(1,-1,0));
}

function bool ConfigureProbeAtStart(RetailHitWallOracleUnrealBot Probe, vector Start)
{
    local vector Goal;
    local vector Direction;
    local vector Perpendicular;
    local vector BlockerLocation;
    local vector FloorLocation;
    local vector FloorNormal;
    local vector HitLocation;
    local vector HitNormal;
    local vector ProbeExtent;
    local vector SamplePosition;
    local Actor FloorActor;
    local Actor WorldActor;
    local Actor PreflightActor;
    local RetailHitWallOracleUnrealBlocker Blocker;
    local int Candidate;
    local int Sample;
    local float LateralOffset;
    local float FloorReferenceZ;

    LateralOffset = 0.0;
    if (OracleCase == 1)
        LateralOffset = 89.0;
    for (Candidate = 0; Candidate < 8; Candidate++)
    {
        Direction = CandidateDirection(Candidate);
        Goal = Start + Direction * 512.0;
        WorldActor = Trace(HitLocation, HitNormal, Goal, Start, false);
        if (WorldActor != None)
            continue;
        for (Sample = 0; Sample <= 4; Sample++)
        {
            SamplePosition = Start + Direction * (128.0 * Sample);
            FloorActor = Trace(FloorLocation, FloorNormal,
                SamplePosition - vect(0,0,256),
                SamplePosition + vect(0,0,96), false);
            if (FloorActor == None || FloorNormal.Z < 0.95)
                break;
            if (Sample == 0)
                FloorReferenceZ = FloorLocation.Z;
            else if (Abs(FloorLocation.Z - FloorReferenceZ) > 16.0)
                break;
        }
        if (Sample <= 4)
            continue;
        Perpendicular.X = -Direction.Y;
        Perpendicular.Y = Direction.X;
        Perpendicular.Z = 0.0;
        BlockerLocation = Start + Direction * 256.0 + Perpendicular * LateralOffset;
        Blocker = Spawn(class'RetailHitWallOracleUnrealBlocker',,, BlockerLocation);
        if (Blocker == None)
            continue;
        Blocker.SetCollision(True, True, True);
        if (Blocker.SetLocation(BlockerLocation) == False)
        {
            Blocker.SetCollision(False, False, False);
            Blocker.Destroy();
            continue;
        }
        ProbeExtent.X = Probe.CollisionRadius;
        ProbeExtent.Y = Probe.CollisionRadius;
        ProbeExtent.Z = Probe.CollisionHeight;
        PreflightActor = Probe.Trace(HitLocation, HitNormal, Goal, Start, True, ProbeExtent);
        if (PreflightActor != Blocker)
        {
            Blocker.SetCollision(False, False, False);
            Blocker.Destroy();
            continue;
        }
        Blocker.ConfigureOracle(Probe, OracleRunId);
        LogOracle("preflight_blocker", "case=" $ OracleCase $ ";candidate=" $ Candidate
            $ ";start=" $ Start $ ";goal=" $ Goal $ ";blocker=" $ Blocker
            $ ";blocker_location=" $ Blocker.Location $ ";normal=" $ HitNormal
            $ ";location=" $ HitLocation);
        Probe.ConfigureOracle(float(OracleMinHitWallMilli) / 1000.0,
            Start, Goal, OracleCase, Blocker, OracleRunId);
        return True;
    }
    return False;
}

function bool ConfigureProbe(RetailHitWallOracleUnrealBot Probe)
{
    local vector InitialStart;
    local PlayerStart CandidateStart;

    InitialStart = Probe.Location;
    if (ConfigureProbeAtStart(Probe, InitialStart))
        return True;
    foreach AllActors(class'PlayerStart', CandidateStart)
    {
        if (VSize(CandidateStart.Location - InitialStart) > 1.0
            && ConfigureProbeAtStart(Probe, CandidateStart.Location))
            return True;
    }
    LogOracle("setup_rejected", "case=" $ OracleCase $ ";start=" $ InitialStart);
    return False;
}

function PostBeginPlay()
{
    local Pawn P;

    Super.PostBeginPlay();
    InitialBots = 0;
    RemainingBots = 0;
    bMultiPlayerBots = False;
    bChangeLevels = False;
    BotConfig.Difficulty = 3;
    BotConfig.bAdjustSkill = False;
    BotConfig.bRandomOrder = False;
    if (AddBot() == False)
    {
        LogOracle("spawn_failed", "");
        return;
    }
    for (P = Level.PawnList; P != None; P = P.NextPawn)
    {
        OracleProbe = RetailHitWallOracleUnrealBot(P);
        if (OracleProbe != None)
        {
            break;
        }
    }
    if (OracleProbe == None)
        LogOracle("probe_missing", "");
    // The spawned bot's own PostBeginPlay/state initialization runs after the
    // game callback. Defer the state transition until the next tick.
    SetTimer(0.10, True);
}

function Timer()
{
    Super.Timer();
    if (bOracleSetupAttempted == False && OracleProbe != None)
    {
        bOracleSetupAttempted = True;
        bOracleStarted = ConfigureProbe(OracleProbe);
    }
    if (bGameEnded == False && Level.TimeSeconds >= OracleDurationSeconds)
    {
        if (!bOracleCompleted)
        {
            LogOracle("oracle_complete", "outcome=game_deadline;case=" $ OracleCase);
            bOracleCompleted = True;
            FlushOracleFallback();
        }
        EndGame("minhitwall_oracle_timeout");
    }
}

defaultproperties
{
    BotConfigType=class'RetailHitWallOracleUnrealBotConfig'
    OracleMinHitWallMilli=-500
    OracleCase=0
    OracleDurationSeconds=6
    FragLimit=0
    TimeLimit=0
    bDontRestart=True
    bLocalLog=True
    bWorldLog=False
}
