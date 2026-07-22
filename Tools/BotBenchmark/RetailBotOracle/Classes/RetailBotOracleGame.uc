// Minimal match controller for observing stock UT 436 bots in a dedicated server.
// It does not replace or modify Botpack.Bot; it only starts an unattended match.
class RetailBotOracleGame extends DeathMatchPlus
    config(RetailBotOracle);

var config int OracleBots;
var config int OracleSkill;
var config int OracleDurationSeconds;
var config bool OracleTouchProbe;
var int OracleElapsedSeconds;

event InitGame(string Options, out string Error)
{
    Super.InitGame(Options, Error);
    OracleBots = GetIntOption(Options, "OracleBots", OracleBots);
    OracleSkill = GetIntOption(Options, "OracleSkill", OracleSkill);
    OracleDurationSeconds = GetIntOption(Options, "OracleDurationSeconds", OracleDurationSeconds);
    OracleTouchProbe = bool(GetIntOption(Options, "OracleTouchProbe", int(OracleTouchProbe)));
}

event PostBeginPlay()
{
    local int BotIndex;

    Super.PostBeginPlay();

    // Dedicated DeathMatchPlus normally waits for a human connection. The oracle
    // needs no participant pawn: configure the stock Botpack game and start it.
    MinPlayers = OracleBots;
    RemainingBots = 0;
    bRequireReady = False;
    bNetReady = False;
    bTournament = False;
    bChangeLevels = False;
    BotConfig.Difficulty = Clamp(OracleSkill, 0, 7);
    BotConfig.bAdjustSkill = False;
    BotConfig.bRandomOrder = False;

    for (BotIndex = 0; BotIndex < OracleBots; BotIndex++)
        AddBot();

    StartMatch();
    if (OracleTouchProbe)
        RunTouchProbe();
    OracleElapsedSeconds = 0;
    LogOracleSnapshots();
    SetTimer(1.0, True);
}

function RunTouchProbe()
{
    local Bot B;
    local Pawn P;
    local HealthVial V;
    local Actor A;
    local bool OldBlockActors;
    local bool OldBlockPlayers;
    local int I;
    local int Spawned;
    local int TouchingCount;
    local string TouchingStates;
    local vector ProbeLocation;

    for (P = Level.PawnList; P != None; P = P.NextPawn)
        if (P.IsA('Bot'))
        {
            B = Bot(P);
            break;
        }
    if (B == None)
        return;

    ProbeLocation = B.Location;
    OldBlockActors = B.bBlockActors;
    OldBlockPlayers = B.bBlockPlayers;
    B.SetCollision(False, OldBlockActors, OldBlockPlayers);
    B.Health = 100;

    for (I = 0; I < 5; I++)
    {
        V = Spawn(class'RetailTouchProbeVial',,, ProbeLocation + vect(1,0,0) * ((I - 2) * 4));
        if (V != None)
            Spawned++;
    }

    B.SetCollision(True, OldBlockActors, OldBlockPlayers);
    B.SetLocation(ProbeLocation);

    for (I = 0; I < 4; I++)
    {
        A = B.Touching[I];
        if (A == None)
            continue;
        TouchingCount++;
        if (TouchingStates != "")
            TouchingStates = TouchingStates $ ">";
        TouchingStates = TouchingStates $ A.Name $ "[state=" $ A.GetStateName()
            $ ",hidden=" $ A.bHidden $ "]";
    }

    if (LocalLog != None)
        LocalLog.LogEventString(
            LocalLog.GetTimeStamp() $ Chr(9) $ "oracle_touch_probe" $ Chr(9)
            $ B.Health $ Chr(9) $ Spawned $ Chr(9) $ TouchingCount $ Chr(9)
            $ TouchingStates);
}

function Timer()
{
    OracleElapsedSeconds++;
    GameReplicationInfo.ElapsedTime = OracleElapsedSeconds;
    LogOracleSnapshots();
    if (!bGameEnded && OracleElapsedSeconds >= OracleDurationSeconds)
        EndGame("oracle_timeout");
}

function bool SetEndCams(string Reason)
{
    // Stock deathmatch enters sudden-death overtime when the score is tied.
    // A duration oracle must close its log at the requested wall-clock bound.
    if (Reason ~= "oracle_timeout")
        return True;
    return Super.SetEndCams(Reason);
}

function LogOracleSnapshots()
{
    local Pawn P;
    local Inventory Inv;
    local int InventoryCount;
    local int WeaponCount;
    local int AmmoTotal;
    local int ArmorTotal;
    local string SelectedWeapon;

    if (LocalLog == None)
        return;

    for (P = Level.PawnList; P != None; P = P.NextPawn)
    {
        if (!P.IsA('Bot') || P.PlayerReplicationInfo == None)
            continue;

        InventoryCount = 0;
        WeaponCount = 0;
        AmmoTotal = 0;
        ArmorTotal = 0;
        for (Inv = P.Inventory; Inv != None; Inv = Inv.Inventory)
        {
            InventoryCount++;
            if (Inv.IsA('Weapon'))
                WeaponCount++;
            if (Inv.IsA('Ammo'))
                AmmoTotal += Ammo(Inv).AmmoAmount;
            if (Inv.bIsAnArmor)
                ArmorTotal += Inv.Charge;
        }

        if (P.Weapon == None)
            SelectedWeapon = "None";
        else
            SelectedWeapon = string(P.Weapon.Class);

        LocalLog.LogEventString(
            LocalLog.GetTimeStamp() $ Chr(9) $ "oracle_snapshot" $ Chr(9)
            $ P.PlayerReplicationInfo.PlayerID $ Chr(9)
            $ P.Health $ Chr(9)
            $ InventoryCount $ Chr(9)
            $ WeaponCount $ Chr(9)
            $ AmmoTotal $ Chr(9)
            $ ArmorTotal $ Chr(9)
            $ SelectedWeapon $ Chr(9)
            $ P.GetStateName() $ Chr(9)
            $ P.Location);
    }
}

defaultproperties
{
    OracleBots=2
    OracleSkill=7
    OracleDurationSeconds=60
    OracleTouchProbe=False
    FragLimit=0
    TimeLimit=0
    bLocalLog=True
    bWorldLog=False
}
