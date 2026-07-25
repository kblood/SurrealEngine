// Selects the probe pawn while leaving DeathMatchPlus spawning and
// initialization untouched. This package is compiled only into an isolated
// retail runtime created by the oracle runner.
class RetailHitWallOracleUTBotConfig extends ChallengeBotInfo;

function class<Bot> CHGetBotClass(int N)
{
    return class'RetailHitWallOracleUTBot';
}
