// Runtime-only dynamic collision target for the Unreal Gold MinHitWall oracle.
class RetailHitWallOracleUnrealBlocker extends BlockAll;

var Actor OracleProbe;
var string OracleRunId;
var int OracleBumpCount;

function LogOracle(string EventName, string Detail)
{
    local string Record;

    Record = "0.00" $ Chr(9) $ "minhitwall_oracle" $ Chr(9) $ EventName $ Chr(9)
        $ "run=" $ OracleRunId $ ";seq="
        $ RetailHitWallOracleUnrealGame(Level.Game).NextOracleSequence() $ ";" $ Detail;
    if (Level.Game != None && Level.Game.LocalLog != None)
        Level.Game.LocalLog.LogEventString(Record);
    Log(Record);
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
