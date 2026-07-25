// Dedicated-state Unreal Gold physics oracle. This records native walking
// HitWall eligibility without claiming to measure normal bot decision quality.
class RetailHitWallOracleUnrealBot extends UnrealI.MaleOneBot;

var float OracleMinHitWall;
var vector OracleStart;
var vector OracleGoal;
var int OracleCase;
var int OracleHitCount;
var bool bOracleConfigured;
var Actor OracleBlocker;
var string OracleRunId;

function LogOracle(string EventName, string Detail)
{
    local string Record;

    Record = "0.00" $ Chr(9) $ "minhitwall_oracle" $ Chr(9) $ EventName $ Chr(9)
        $ "run=" $ OracleRunId $ ";seq="
        $ RetailHitWallOracleUnrealGame(Level.Game).NextOracleSequence() $ ";" $ Detail;
    if (Level.Game != None && Level.Game.LocalLog != None)
        Level.Game.LocalLog.LogEventString(Record);
    // Retail 226b may not instantiate LocalLog on a dedicated UCC server.
    // Mirror every oracle event to the isolated process log for that profile.
    Log(Record);
}

function ConfigureOracle(float InMinHitWall, vector InStart, vector InGoal,
    int InCase, Actor InBlocker, string InRunId)
{
    OracleMinHitWall = InMinHitWall;
    OracleStart = InStart;
    OracleGoal = InGoal;
    OracleCase = InCase;
    OracleBlocker = InBlocker;
    OracleRunId = InRunId;
    bOracleConfigured = True;
    GotoState('OracleProbe');
}

function LogPostflightBlocker()
{
    local vector PostHitLocation;
    local vector PostHitNormal;
    local vector PostExtent;
    local Actor PostflightActor;

    if (OracleBlocker == None)
        return;
    PostExtent.X = CollisionRadius;
    PostExtent.Y = CollisionRadius;
    PostExtent.Z = CollisionHeight;
    PostflightActor = Trace(PostHitLocation, PostHitNormal, OracleBlocker.Location,
        Location, True, PostExtent);
    LogOracle("postflight_blocker", "actor=" $ PostflightActor
        $ ";expected=" $ OracleBlocker $ ";normal=" $ PostHitNormal
        $ ";location=" $ PostHitLocation);
}

function CompleteOracle(string Outcome)
{
    LogOracle("oracle_complete", "outcome=" $ Outcome);
    if (RetailHitWallOracleUnrealGame(Level.Game) != None)
    {
        RetailHitWallOracleUnrealGame(Level.Game).MarkOracleComplete();
        RetailHitWallOracleUnrealGame(Level.Game).FlushOracleFallback();
    }
}

state OracleProbe
{
    function Bump(Actor Other)
    {
        if (Other == OracleBlocker)
            LogOracle("probe_bump", "other=" $ Other $ ";location=" $ Location);
    }

    function HitWall(vector HitNormal, actor Wall)
    {
        local float NormalVelocityDot;
        local bool bDoorHandled;
        local bool bWallAdjusted;

        OracleHitCount++;
        if (VSize(Velocity) > 0.001)
            NormalVelocityDot = HitNormal dot Normal(Velocity);
        else
            NormalVelocityDot = 2.0;
        LogOracle("hitwall_pre",
            "case=" $ OracleCase $ ";count=" $ OracleHitCount
            $ ";min=" $ OracleMinHitWall $ ";dot=" $ NormalVelocityDot
            $ ";normal=" $ HitNormal $ ";velocity=" $ Velocity
            $ ";location=" $ Location $ ";wall=" $ Wall $ ";physics=" $ Physics
            $ ";state=" $ GetStateName());
        if (Physics == PHYS_Falling)
        {
            LogOracle("hitwall_falling_return", "case=" $ OracleCase);
            CompleteOracle("falling_callback");
            GotoState('OracleFinished');
            return;
        }
        if (Wall.IsA('Mover'))
        {
            LogOracle("handle_door_pre", "case=" $ OracleCase $ ";wall=" $ Wall);
            bDoorHandled = Mover(Wall).HandleDoor(self);
            LogOracle("handle_door_post", "case=" $ OracleCase $ ";handled=" $ bDoorHandled
                $ ";special_pause=" $ SpecialPause $ ";move_target=" $ MoveTarget);
            if (bDoorHandled)
            {
                LogOracle("pick_wall_adjust_skipped", "case=" $ OracleCase);
                CompleteOracle("mover_handled");
                GotoState('OracleFinished');
                return;
            }
        }
        Focus = Destination;
        bWallAdjusted = PickWallAdjust();
        LogOracle("pick_wall_adjust_result",
            "case=" $ OracleCase $ ";adjusted=" $ bWallAdjusted
            $ ";physics=" $ Physics $ ";move_timer=" $ MoveTimer);
        if (!bWallAdjusted)
            MoveTimer = -1.0;
        CompleteOracle("callback");
        GotoState('OracleFinished');
    }

Begin:
    if (bOracleConfigured == False)
    {
        LogOracle("configuration_missing", "");
        Stop;
    }
    Sleep(0.25);
    if (SetLocation(OracleStart) == False)
    {
        LogOracle("start_rejected", "case=" $ OracleCase $ ";start=" $ OracleStart);
        CompleteOracle("start_rejected");
        Stop;
    }
    SetPhysics(PHYS_Walking);
    MinHitWall = OracleMinHitWall;
    Destination = OracleGoal;
    Focus = Destination;
    LogOracle("move_begin", "case=" $ OracleCase $ ";min=" $ MinHitWall
        $ ";start=" $ Location $ ";goal=" $ Destination);
    MoveTo(Destination);
    LogPostflightBlocker();
    LogOracle("move_return", "case=" $ OracleCase $ ";hits=" $ OracleHitCount
        $ ";location=" $ Location $ ";physics=" $ Physics $ ";move_timer=" $ MoveTimer);
    CompleteOracle("move_return");
    GotoState('OracleFinished');
}

state OracleFinished
{
Begin:
    Velocity = vect(0,0,0);
    Acceleration = vect(0,0,0);
    Stop;
}
