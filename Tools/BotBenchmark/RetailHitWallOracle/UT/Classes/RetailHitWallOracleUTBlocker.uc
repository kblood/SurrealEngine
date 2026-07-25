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
    if (Other == OracleProbe)
    {
        OracleBumpCount++;
        LogOracle("blocker_bump", "count=" $ OracleBumpCount $ ";other=" $ Other
            $ ";location=" $ Location);
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
