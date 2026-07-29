// Runtime-only dynamic collision target for the MinHitWall physics oracle.
// It is never installed into a retail game and does not model mover behavior.
class RetailHitWallOracleUTBlocker extends BlockAll;

var Actor OracleProbe;
var string OracleRunId;
var int OracleBumpCount;
var int OracleTouchCount;

function LogOracle(string EventName, string Detail)
{
    if (Level.Game != None && Level.Game.LocalLog != None)
        Level.Game.LocalLog.LogEventString(
            Level.Game.LocalLog.GetTimeStamp() $ Chr(9) $ "minhitwall_oracle" $ Chr(9)
            $ EventName $ Chr(9) $ "run=" $ OracleRunId $ ";seq="
            $ RetailHitWallOracleUTGame(Level.Game).NextOracleSequence() $ ";" $ Detail);
}

function ConfigureOracle(Actor InProbe, string InRunId)
{
    OracleProbe = InProbe;
    OracleRunId = InRunId;
}

function Bump(Actor Other)
{
    local vector TraceLocation;
    local vector TraceNormal;
    local vector TraceExtent;
    local Actor TraceActor;
    local Pawn Probe;
    local RetailHitWallOracleUTBot OracleProbeBot;

    if (Other == OracleProbe)
    {
        Probe = Pawn(OracleProbe);
        if (Probe == None)
            return;
        OracleProbeBot = RetailHitWallOracleUTBot(OracleProbe);
        OracleBumpCount++;
        TraceExtent.X = Probe.CollisionRadius;
        TraceExtent.Y = Probe.CollisionRadius;
        TraceExtent.Z = Probe.CollisionHeight;
        TraceActor = Probe.Trace(TraceLocation, TraceNormal,
            Probe.Destination, Probe.Location, True, TraceExtent);
        LogOracle("blocker_bump", "bump_index=" $ OracleBumpCount $ ";other=" $ Other
            $ ";min=" $ Probe.MinHitWall $ ";velocity=" $ Probe.Velocity
            $ ";acceleration=" $ Probe.Acceleration $ ";physics=" $ Probe.Physics
            $ ";location=" $ Location $ ";probe_location=" $ Probe.Location
            $ ";probe_radius=" $ Probe.CollisionRadius $ ";probe_height=" $ Probe.CollisionHeight
            $ ";blocker_radius=" $ CollisionRadius $ ";blocker_height=" $ CollisionHeight
            $ ";center_normal_reconstructed=" $ Normal(Probe.Location - Location)
            $ ";bump_trace_actor=" $ TraceActor $ ";bump_trace_normal=" $ TraceNormal
            $ ";bump_trace_location=" $ TraceLocation
            $ OracleProbeBot.OracleThresholdDetail());
    }
}

function Touch(Actor Other)
{
    if (Other == OracleProbe)
    {
        OracleTouchCount++;
        LogOracle("blocker_touch", "count=" $ OracleTouchCount $ ";other=" $ Other
            $ ";location=" $ Location);
    }
}

defaultproperties
{
    bStatic=False
    bNoDelete=False
    bHidden=True
    bAlwaysRelevant=False
    RemoteRole=ROLE_None
    CollisionRadius=80.000000
    CollisionHeight=96.000000
}
