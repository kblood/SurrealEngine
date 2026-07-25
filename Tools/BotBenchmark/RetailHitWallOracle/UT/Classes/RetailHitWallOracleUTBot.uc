// Dedicated-state retail physics oracle. This is intentionally not a stock-AI
// behavior test: the state isolates native walking/HitWall eligibility and
// records exactly what the retail engine delivers to UnrealScript.
class RetailHitWallOracleUTBot extends TMale1Bot;

var float OracleMinHitWall;
var vector OracleStart;
var vector OracleGoal;
var int OracleCase;
var int OracleHitCount;
var bool bOracleConfigured;

function LogOracle(string EventName, string Detail)
{
    if (Level.Game != None && Level.Game.LocalLog != None)
        Level.Game.LocalLog.LogEventString(
            Level.Game.LocalLog.GetTimeStamp() $ Chr(9) $ "minhitwall_oracle" $ Chr(9)
            $ EventName $ Chr(9) $ Detail);
}

function ConfigureOracle(float InMinHitWall, vector InStart, vector InGoal, int InCase)
{
    OracleMinHitWall = InMinHitWall;
    OracleStart = InStart;
    OracleGoal = InGoal;
    OracleCase = InCase;
    bOracleConfigured = True;
    GotoState('OracleProbe');
}

state OracleProbe
{
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
            $ ";location=" $ Location $ ";wall=" $ Wall $ ";state=" $ GetStateName());

        // One raw contact is the unit of evidence. Do not loop on the same
        // face while the dedicated server waits for its bounded shutdown.
        // This is the stock Bot state-handler ordering, made explicit only so
        // the isolated probe can record the mover branch.
        if (Physics == PHYS_Falling)
        {
            LogOracle("hitwall_falling_return", "case=" $ OracleCase);
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
    LogOracle("move_return",
        "case=" $ OracleCase $ ";hits=" $ OracleHitCount
        $ ";location=" $ Location $ ";physics=" $ Physics
        $ ";move_timer=" $ MoveTimer);
}

state OracleFinished
{
Begin:
    Velocity = vect(0,0,0);
    Acceleration = vect(0,0,0);
    Stop;
}
