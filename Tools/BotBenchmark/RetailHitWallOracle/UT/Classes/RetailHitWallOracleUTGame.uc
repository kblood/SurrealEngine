// Unattended UT436 runner for a single pinned DM-Deck16][ walking contact.
// It uses the normal DeathMatchPlus BotConfig/AddBot path and runs only inside
// a disposable retail runtime, never from the installed game directory.
class RetailHitWallOracleUTGame extends DeathMatchPlus
    config(RetailHitWallOracleUT);

var config int OracleMinHitWallMilli;
var config int OracleCase;
var config int OracleDurationSeconds;
var bool bOracleStarted;

event InitGame(string Options, out string Error)
{
    Super.InitGame(Options, Error);
    OracleMinHitWallMilli = GetIntOption(Options, "OracleMinHitWallMilli", OracleMinHitWallMilli);
    OracleCase = GetIntOption(Options, "OracleCase", OracleCase);
    OracleDurationSeconds = GetIntOption(Options, "OracleDurationSeconds", OracleDurationSeconds);
}

function LogOracle(string EventName, string Detail)
{
    if (LocalLog != None)
        LocalLog.LogEventString(LocalLog.GetTimeStamp() $ Chr(9)
            $ "minhitwall_oracle" $ Chr(9) $ EventName $ Chr(9) $ Detail);
}

function ConfigureProbe(RetailHitWallOracleUTBot Probe)
{
    local vector Start;
    local vector Goal;

    // The first oracle stage pins the static corridor face south of Mover0.
    // It establishes the native MinHitWall comparator without conflating the
    // result with lift timing. Mover ordering remains a separate gate.
    if (OracleCase == 1)
    {
        // Intended nominal -0.4 glancing target. The emitted contact normal
        // and dot are authoritative; Deck geometry may choose another face.
        Start = vect(1264,1550,-1222);
        Goal = vect(1900,1820,-1222);
    }
    else
    {
        Start = vect(1264,1550,-1222);
        Goal = vect(1264,1900,-1222);
    }
    Probe.ConfigureOracle(float(OracleMinHitWallMilli) / 1000.0,
        Start, Goal, OracleCase);
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
            ConfigureProbe(Probe);
            bOracleStarted = True;
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
        EndGame("minhitwall_oracle_timeout");
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
