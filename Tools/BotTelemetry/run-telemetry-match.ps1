# Launch retail UT99 (v469e) with the BotTelemetry mutator attached, matching the
# SurrealEngine bot benchmark configuration so the two telemetry streams line up.
#
#   16 bots, Difficulty 3, DM-Deck16][, DeathMatchPlus
#
# Output: System\..\Logs\bottelemetry.log  (tab separated, see BotTelemetryMutator.uc)
# The log is renamed from .tmp to .log when the match ends, so let the timelimit
# expire rather than alt-F4'ing out of the match.

param(
    [int]$Bots       = 16,
    [int]$Difficulty = 3,
    [int]$TimeLimit  = 5,
    [string]$Map     = 'DM-Deck16][',
    [double]$SampleHz = 30,
    [bool]$ProbeWaterJump = $true,
    [bool]$ProbeTraceCorpus = $true,
    [bool]$ProbeReachCorpus = $false
)

$MinPlayers = $Bots + 1

$system = Join-Path $PSScriptRoot 'System'
$logs   = Join-Path $PSScriptRoot 'Logs'
$exe    = Join-Path $system 'UnrealTournament.exe'

if (-not (Test-Path $exe)) { throw "UnrealTournament.exe not found at $exe" }
if (-not (Test-Path (Join-Path $system 'BotTelemetry.u'))) {
    throw "BotTelemetry.u not built. Run: System\UCC.exe make"
}
if (-not (Test-Path $logs)) { New-Item -ItemType Directory -Path $logs | Out-Null }

# Archive any previous capture so a partial run can't be mistaken for a fresh one.
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
foreach ($old in @('bottelemetry.log','bottelemetry.tmp')) {
    $p = Join-Path $logs $old
    if (Test-Path $p) { Move-Item $p (Join-Path $logs "$old.$stamp.bak") -Force }
}

# SampleHz is read from BotTelemetry.ini; write it so the rate is reproducible.
$ini = Join-Path $system 'BotTelemetry.ini'
@(
    '[BotTelemetry.BotTelemetryMutator]'
    "SampleHz=$SampleHz"
    'bLogDamage=True'
    'bBotsOnly=False'
    "bProbeWaterJump=$(if ($ProbeWaterJump) {'True'} else {'False'})"
    "bProbeTraceCorpus=$(if ($ProbeTraceCorpus) {'True'} else {'False'})"
    "bProbeReachCorpus=$(if ($ProbeReachCorpus) {'True'} else {'False'})"
    'ProbeReachDist=1000.000000'
    'ProbeRadius=17.000000'
    'ProbeHeight=39.000000'
    'ProbeDist=60.000000'
) | Set-Content -Path $ini -Encoding ASCII

$url = "$Map`?Game=Botpack.DeathMatchPlus`?MinPlayers=$MinPlayers`?Difficulty=$Difficulty`?TimeLimit=$TimeLimit`?FragLimit=0`?Mutator=BotTelemetry.BotTelemetryMutator"

Write-Host "Launching: $url"
Write-Host "Sampling at $SampleHz Hz -> $logs\bottelemetry.log"
Write-Host "Let the $TimeLimit-minute timelimit expire so the log is finalised."

Start-Process -FilePath $exe -WorkingDirectory $system -ArgumentList @($url)
