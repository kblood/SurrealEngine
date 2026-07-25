// Unattended UT436 runner for a single pinned DM-Deck16][ walking contact.
// It uses the normal DeathMatchPlus BotConfig/AddBot path and runs only inside
// a disposable retail runtime, never from the installed game directory.
class RetailHitWallOracleUTGame extends DeathMatchPlus
    config(RetailHitWallOracleUT);

var config int OracleMinHitWallMilli;
var config int OracleCase;
var config int OracleDurationSeconds;
var bool bOracleStarted;
var bool bOracleCompleted;
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
    if (LocalLog != None)
        LocalLog.LogEventString(LocalLog.GetTimeStamp() $ Chr(9)
            $ "minhitwall_oracle" $ Chr(9) $ EventName $ Chr(9)
            $ "run=" $ OracleRunId $ ";seq=" $ NextOracleSequence() $ ";" $ Detail);
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

function bool ConfigureProbeAtStart(RetailHitWallOracleUTBot Probe, vector Start)
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
    local Actor PreflightActor;
    local RetailHitWallOracleUTBlocker Blocker;
    local int Candidate;
    local int Sample;
    local float LateralOffset;

    LateralOffset = 0.0;
    if (OracleCase == 1)
        LateralOffset = 89.0;

    for (Candidate = 0; Candidate < 8; Candidate++)
    {
        Direction = CandidateDirection(Candidate);
        Goal = Start + Direction * 512.0;
        if (!FastTrace(Goal, Start))
            continue;
        for (Sample = 0; Sample <= 4; Sample++)
        {
            SamplePosition = Start + Direction * (128.0 * Sample);
            FloorActor = Trace(FloorLocation, FloorNormal,
                SamplePosition - vect(0,0,256),
                SamplePosition + vect(0,0,96), false);
            if (FloorActor == None || FloorNormal.Z < 0.7)
                break;
        }
        if (Sample <= 4)
            continue;
        Perpendicular.X = -Direction.Y;
        Perpendicular.Y = Direction.X;
        Perpendicular.Z = 0.0;
        BlockerLocation = Start + Direction * 256.0 + Perpendicular * LateralOffset;
        Blocker = Spawn(class'RetailHitWallOracleUTBlocker',,, BlockerLocation);
        if (Blocker == None)
            continue;
        Blocker.SetCollision(True, True, True);
        if (!Blocker.SetLocation(BlockerLocation))
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
        LogOracle("preflight_blocker", "case=" $ OracleCase
            $ ";candidate=" $ Candidate $ ";start=" $ Start $ ";goal=" $ Goal
            $ ";blocker=" $ Blocker $ ";blocker_location=" $ Blocker.Location
            $ ";normal=" $ HitNormal $ ";location=" $ HitLocation);
        Probe.ConfigureOracle(float(OracleMinHitWallMilli) / 1000.0,
            Start, Goal, OracleCase, Blocker, OracleRunId);
        return True;
    }
    return False;
}

function bool ConfigureProbe(RetailHitWallOracleUTBot Probe)
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
    local RetailHitWallOracleUTBot Probe;

    Super.PostBeginPlay();
    MinPlayers = 0;
    RemainingBots = 0;
    bRequireReady = False;
    bNetReady = False;
    bTournament = False;
    bChangeLevels = False;
    bRatedGame = False;
    BotConfig.Difficulty = 3;
    BotConfig.bAdjustSkill = False;
    BotConfig.bRandomOrder = False;

    if (!AddBot())
    {
        LogOracle("spawn_failed", "");
        return;
    }
    for (P = Level.PawnList; P != None; P = P.NextPawn)
    {
        Probe = RetailHitWallOracleUTBot(P);
        if (Probe != None)
        {
            bOracleStarted = ConfigureProbe(Probe);
            break;
        }
    }
    if (!bOracleStarted)
        LogOracle("probe_missing", "");
    StartMatch();
    SetTimer(1.0, True);
}

function Timer()
{
    Super.Timer();
    if (!bGameEnded && Level.TimeSeconds >= OracleDurationSeconds)
    {
        if (!bOracleCompleted)
        {
            LogOracle("oracle_complete", "outcome=game_deadline;case=" $ OracleCase);
            bOracleCompleted = True;
        }
        EndGame("minhitwall_oracle_timeout");
    }
}

defaultproperties
{
    BotConfigType=class'RetailHitWallOracleUTBotConfig'
    OracleMinHitWallMilli=-500
    OracleCase=0
    OracleDurationSeconds=6
    FragLimit=0
    TimeLimit=0
    bLocalLog=True
    bWorldLog=False
}
