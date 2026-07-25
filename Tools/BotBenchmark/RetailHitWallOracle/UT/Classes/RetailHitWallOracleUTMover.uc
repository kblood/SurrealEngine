// Runtime-only TriggerOpenTimed mover used to observe Bot.HandleDoor ordering.
class RetailHitWallOracleUTMover extends Mover;

var string OracleRunId;

function LogOracle(string EventName, string Detail)
{
    if (Level.Game != None && Level.Game.LocalLog != None)
        Level.Game.LocalLog.LogEventString(
            Level.Game.LocalLog.GetTimeStamp() $ Chr(9) $ "minhitwall_oracle" $ Chr(9)
            $ EventName $ Chr(9) $ "run=" $ OracleRunId $ ";seq="
            $ RetailHitWallOracleUTGame(Level.Game).NextOracleSequence() $ ";" $ Detail);
}

function ConfigureOracle(string InRunId)
{
    OracleRunId = InRunId;
}

state TriggerOpenTimed
{
    function bool HandleDoor(pawn Other)
    {
        local bool bHandled;

        LogOracle("mover_handle_door_enter", "other=" $ Other $ ";state=" $ GetStateName());
        bHandled = Super.HandleDoor(Other);
        LogOracle("mover_handle_door_return", "handled=" $ bHandled
            $ ";waiting_pawn=" $ WaitingPawn $ ";special_pause=" $ Other.SpecialPause);
        return bHandled;
    }
}

defaultproperties
{
    bStatic=False
    bNoDelete=False
    bHidden=True
    bAlwaysRelevant=False
    RemoteRole=ROLE_None
    bCollideActors=True
    bBlockActors=True
    bBlockPlayers=True
    CollisionRadius=80.000000
    CollisionHeight=96.000000
    BumpType=BT_AnyBump
    bUseTriggered=True
    DelayTime=60.000000
    InitialState=TriggerOpenTimed
}
