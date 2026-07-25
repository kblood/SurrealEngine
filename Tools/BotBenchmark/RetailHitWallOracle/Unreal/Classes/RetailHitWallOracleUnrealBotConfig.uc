// Unreal Gold 226b uses BotInfo/GetBotClass rather than UT's ChallengeBotInfo.
class RetailHitWallOracleUnrealBotConfig extends UnrealShare.BotInfo;

function class<Bots> GetBotClass(int N)
{
    return class'RetailHitWallOracleUnrealBot';
}
