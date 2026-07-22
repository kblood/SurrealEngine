[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$SurrealSummary,
    [Parameter(Mandatory)][string]$RetailSummary,
    [string]$Output
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$surreal = Get-Content -Raw -LiteralPath $SurrealSummary | ConvertFrom-Json
$retail = Get-Content -Raw -LiteralPath $RetailSummary | ConvertFrom-Json
$surrealBots = @($surreal.bot_metrics)
$retailBots = @($retail.bots)
if ($surrealBots.Count -eq 0 -or $retailBots.Count -eq 0) { throw 'Both summaries must contain per-bot metrics.' }

function Average([object[]]$Values) {
    $numbers = @($Values | Where-Object { $null -ne $_ } | ForEach-Object { [double]$_ })
    if ($numbers.Count -eq 0) { return $null }
    return [Math]::Round(($numbers | Measure-Object -Average).Average, 4)
}

$surrealDuration = [double]$surreal.simulated_seconds
$surrealKills = [double](($surrealBots.maximum_pawn_kill_count | Measure-Object -Sum).Sum)
$retailKills = [double]$retail.kills
$surrealFirstWeapon = @($surrealBots | ForEach-Object {
    if ($null -ne $_.first_nonstarter_weapon_tick -and [int64]$_.first_nonstarter_weapon_tick -ge 0) {
        [double]$_.first_nonstarter_weapon_tick * [double]$surreal.fixed_delta
    }
})
$retailFirstWeapon = @($retailBots | ForEach-Object { $_.first_extra_weapon_seconds })

$rows = @(
    [pscustomobject]@{ metric='first_extra_weapon_seconds_mean'; retail=(Average $retailFirstWeapon); surreal=(Average $surrealFirstWeapon); direction='lower_is_better'; note='Retail timing is sampled at one-second intervals.' },
    [pscustomobject]@{ metric='maximum_weapon_count_mean'; retail=(Average @($retailBots.max_weapon_count)); surreal=(Average @($surrealBots.maximum_weapon_count)); direction='higher_is_better'; note='' },
    [pscustomobject]@{ metric='maximum_ammo_total_mean'; retail=(Average @($retailBots.max_ammo_total)); surreal=(Average @($surrealBots.maximum_useful_ammo)); direction='higher_is_better'; note='Retail counts all inventory ammo; Surreal reports useful ammo.' },
    [pscustomobject]@{ metric='maximum_armor_mean'; retail=(Average @($retailBots.max_armor_total)); surreal=(Average @($surrealBots.maximum_armor)); direction='higher_is_better'; note='' },
    [pscustomobject]@{ metric='kills_per_minute'; retail=[double]$retail.kills_per_minute; surreal=if($surrealDuration -gt 0){[Math]::Round(60*$surrealKills/$surrealDuration,4)}else{0}; direction='higher_is_better'; note='Use multi-run distributions; retail RNG cannot be seeded.' },
    [pscustomobject]@{ metric='deaths_total'; retail=[double](($retailBots.deaths | Measure-Object -Sum).Sum); surreal=[double](($surrealBots.maximum_pri_deaths | Measure-Object -Sum).Sum); direction='context_only'; note='' }
)

foreach ($row in $rows) {
    $row | Add-Member -NotePropertyName surreal_minus_retail -NotePropertyValue $(
        if ($null -ne $row.retail -and $null -ne $row.surreal) { [Math]::Round([double]$row.surreal - [double]$row.retail, 4) } else { $null }
    )
}

$comparison = [ordered]@{
    schema_version = 1
    surreal_summary = (Resolve-Path -LiteralPath $SurrealSummary).Path
    retail_summary = (Resolve-Path -LiteralPath $RetailSummary).Path
    comparable_map = ($surreal.map -eq $retail.map)
    comparable_requested_skill = ([int]$surreal.requested_difficulty -eq [int]$retail.requested_skill)
    comparable_bot_count = ([int]$surreal.requested_bots -eq [int]$retail.requested_bots)
    warning = 'This is a coarse behavioral comparison. Run repeated retail samples and matching Surreal scenarios before drawing conclusions.'
    metrics = $rows
}

$json = $comparison | ConvertTo-Json -Depth 8
if ($Output) {
    $outputPath = [IO.Path]::GetFullPath($Output)
    New-Item -ItemType Directory -Force -Path (Split-Path $outputPath -Parent) | Out-Null
    Set-Content -LiteralPath $outputPath -Value $json -Encoding UTF8
}
$json
