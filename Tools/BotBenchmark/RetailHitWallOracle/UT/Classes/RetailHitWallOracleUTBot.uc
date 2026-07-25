// Dedicated-state retail physics oracle. This is intentionally not a stock-AI
// behavior test: the state isolates native walking/HitWall eligibility and
// records exactly what the retail engine delivers to UnrealScript.
class RetailHitWallOracleUTBot extends TMale1Bot;

var float OracleMinHitWall;
var vector OracleStart;
var vector OracleGoal;
var int OracleCase;
var int OracleHitCount;
var int OracleProbeBumpCount;
var bool bOracleConfigured;
var Actor OracleBlocker;
var string OracleRunId;

function LogOracle(string EventName, string Detail)
{
    if (Level.Game != None && Level.Game.LocalLog != None)
        Level.Game.LocalLog.LogEventString(
            Level.Game.LocalLog.GetTimeStamp() $ Chr(9) $ "minhitwall_oracle" $ Chr(9)
            $ EventName $ Chr(9) $ "run=" $ OracleRunId $ ";seq="
            $ RetailHitWallOracleUTGame(Level.Game).NextOracleSequence() $ ";" $ Detail);
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
    if (RetailHitWallOracleUTGame(Level.Game) != None)
        RetailHitWallOracleUTGame(Level.Game).MarkOracleComplete();
}

state OracleProbe
{
    function Bump(Actor Other)
    {
        local vector TraceLocation;
        local vector TraceNormal;
        local vector TraceExtent;
        local Actor TraceActor;

        if (Other == OracleBlocker)
        {
            OracleProbeBumpCount++;
            TraceExtent.X = CollisionRadius;
            TraceExtent.Y = CollisionRadius;
            TraceExtent.Z = CollisionHeight;
            TraceActor = Trace(TraceLocation, TraceNormal, Destination, Location, True, TraceExtent);
            LogOracle("probe_bump", "bump_index=" $ OracleProbeBumpCount $ ";other=" $ Other $ ";min=" $ MinHitWall
                $ ";velocity=" $ Velocity $ ";acceleration=" $ Acceleration
                $ ";physics=" $ Physics $ ";location=" $ Location
                $ ";blocker_location=" $ Other.Location
                $ ";probe_radius=" $ CollisionRadius $ ";probe_height=" $ CollisionHeight
                $ ";blocker_radius=" $ Other.CollisionRadius $ ";blocker_height=" $ Other.CollisionHeight
                $ ";center_normal_reconstructed=" $ Normal(Location - Other.Location)
                $ ";bump_trace_actor=" $ TraceActor $ ";bump_trace_normal=" $ TraceNormal
                $ ";bump_trace_location=" $ TraceLocation);
        }
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

        // One raw contact is the unit of evidence. Do not loop on the same
        // face while the dedicated server waits for its bounded shutdown.
        // This is the stock Bot state-handler ordering, made explicit only so
        // the isolated probe can record the mover branch.
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
            LogOracle("handle_door_post",
                "case=" $ OracleCase $ ";handled=" $ bDoorHandled
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
    if (!bOracleConfigured)
    {
        LogOracle("configuration_missing", "");
        Stop;
    }
    Sleep(0.25);
    if (!SetLocation(OracleStart))
    {
        LogOracle("start_rejected", "case=" $ OracleCase $ ";start=" $ OracleStart);
        Stop;
    }
    SetPhysics(PHYS_Walking);
    MinHitWall = OracleMinHitWall;
    Destination = OracleGoal;
    Focus = Destination;
    LogOracle("move_begin",
        "case=" $ OracleCase $ ";min=" $ MinHitWall
        $ ";start=" $ Location $ ";goal=" $ Destination);
    MoveTo(Destination);
    LogPostflightBlocker();
    LogOracle("move_return",
        "case=" $ OracleCase $ ";hits=" $ OracleHitCount
        $ ";location=" $ Location $ ";physics=" $ Physics
        $ ";move_timer=" $ MoveTimer);
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
