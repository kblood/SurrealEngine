// Diagnostic-only vial used by the opt-in retail native touch-overflow probe.
// Its pickup behavior remains the stock Botpack.HealthVial behavior.
class RetailTouchProbeVial extends HealthVial;

function LogProbeTouch(string Phase, Actor Other)
{
    local Pawn P;
    local Actor A;
    local int I;
    local int ContactCount;
    local string Contacts;

    P = Pawn(Other);
    if (P == None || Level.Game.LocalLog == None)
        return;

    for (I = 0; I < 4; I++)
    {
        A = P.Touching[I];
        if (A == None)
            continue;
        ContactCount++;
        if (Contacts != "")
            Contacts = Contacts $ ">";
        Contacts = Contacts $ A.Name $ "[state=" $ A.GetStateName()
            $ ",hidden=" $ A.bHidden $ "]";
    }

    Level.Game.LocalLog.LogEventString(
        Level.Game.LocalLog.GetTimeStamp() $ Chr(9) $ "oracle_touch_step" $ Chr(9)
        $ Phase $ Chr(9) $ Name $ Chr(9) $ P.Health $ Chr(9)
        $ ContactCount $ Chr(9) $ Contacts);
}

auto state Pickup
{
    function Touch(Actor Other)
    {
        LogProbeTouch("before", Other);
        Super.Touch(Other);
        LogProbeTouch("after", Other);
    }
}

