[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string] $GameRoot,

    [Parameter(Mandatory = $true)]
    [string] $OutputRoot,

    [string] $EnginePath = (Join-Path $PSScriptRoot '..\..\build\Release\SurrealEngine.exe'),

    [string[]] $Maps = @('DM-Morbias][', 'DM-Deck16]['),

    [int[]] $Skills = @(0, 3, 4, 6, 7),

    [string[]] $Seeds = @('104729', '271828'),

    [ValidateRange(1, 32)]
    [int] $Bots = 2,

    [int] $OpponentSkill = -1,

    [ValidateRange(1, 100)]
    [int] $RunsPerCase = 2,

    [ValidateRange(0.1, 86400.0)]
    [double] $Seconds = 30.0,

    [ValidateRange(0.000001, 1.0)]
    [double] $FixedDelta = (1.0 / 60.0),

    [ValidateRange(1, 86400)]
    [int] $TimeoutSeconds = 120,

    [string] $BotName = 'Loque',

    [string] $FixtureId = ''
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0

function Resolve-ExistingPath {
    param(
        [Parameter(Mandatory = $true)] [string] $Path,
        [Parameter(Mandatory = $true)] [string] $Description,
        [switch] $Leaf
    )

    if ([string]::IsNullOrWhiteSpace($Path)) {
        throw "$Description path must not be empty."
    }

    $item = Get-Item -LiteralPath $Path -ErrorAction Stop
    if ($Leaf -and $item.PSIsContainer) {
        throw "$Description must be a file: $Path"
    }
    if (-not $Leaf -and -not $item.PSIsContainer) {
        throw "$Description must be a directory: $Path"
    }
    return $item.FullName
}

function ConvertTo-ProcessArgument {
    param([Parameter(Mandatory = $true)] [string] $Value)

    # Start-Process joins ArgumentList values into one Windows command line.
    # The matrix inputs do not need embedded quotes; rejecting them keeps the
    # quoting rule auditable while still supporting paths containing spaces.
    if ($Value.Contains('"')) {
        throw "Arguments containing a double quote are not supported: $Value"
    }
    if ($Value -match '\s') {
        return '"' + $Value + '"'
    }
    return $Value
}

function ConvertTo-SafeName {
    param([Parameter(Mandatory = $true)] [string] $Value)

    $safe = $Value -replace '[^A-Za-z0-9._-]', '_'
    $safe = $safe.Trim('._-')
    if ([string]::IsNullOrWhiteSpace($safe)) {
        throw "Value cannot be converted to a safe directory name: $Value"
    }
    return $safe
}

function Add-ValidationError {
    param(
        [Parameter(Mandatory = $true)]
        [AllowEmptyCollection()]
        [System.Collections.Generic.List[string]] $Errors,
        [Parameter(Mandatory = $true)]
        [string] $Message
    )
    $Errors.Add($Message)
}

$resolvedEnginePath = Resolve-ExistingPath -Path $EnginePath -Description 'SurrealEngine executable' -Leaf
$resolvedGameRoot = Resolve-ExistingPath -Path $GameRoot -Description 'UE1 game root'

if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
    throw 'OutputRoot is mandatory and must not be empty.'
}
if ($OutputRoot.Contains('"')) {
    throw 'OutputRoot must not contain a double quote.'
}

$resolvedOutputRoot = [IO.Path]::GetFullPath($OutputRoot)
$outputPathRoot = [IO.Path]::GetPathRoot($resolvedOutputRoot)
if ($resolvedOutputRoot.TrimEnd('\', '/') -eq $outputPathRoot.TrimEnd('\', '/')) {
    throw "Refusing to use a drive root as OutputRoot: $resolvedOutputRoot"
}
if (Test-Path -LiteralPath $resolvedOutputRoot -PathType Leaf) {
    throw "OutputRoot points to a file: $resolvedOutputRoot"
}
New-Item -ItemType Directory -Path $resolvedOutputRoot -Force | Out-Null

$normalizedMaps = New-Object System.Collections.Generic.List[string]
foreach ($mapValue in $Maps) {
    if ([string]::IsNullOrWhiteSpace($mapValue)) {
        throw 'Map names must not be empty.'
    }
    $map = [IO.Path]::GetFileNameWithoutExtension($mapValue.Trim())
    if ($map -notmatch '^[A-Za-z0-9_.\[\]-]+$') {
        throw "Map name contains unsupported characters: $mapValue"
    }
    if (-not $normalizedMaps.Contains($map)) {
        $normalizedMaps.Add($map)
    }
}
if ($normalizedMaps.Count -eq 0) {
    throw 'At least one map is required.'
}

$normalizedSkills = @($Skills | Sort-Object -Unique)
if ($normalizedSkills.Count -eq 0) {
    throw 'At least one skill is required.'
}
foreach ($skill in $normalizedSkills) {
    if ($skill -lt 0 -or $skill -gt 7) {
        throw "Skill must be in [0, 7]: $skill"
    }
}

if ($OpponentSkill -ne -1) {
    if ($OpponentSkill -lt 0 -or $OpponentSkill -gt 7) {
        throw "OpponentSkill must be -1 (disabled) or in [0, 7]: $OpponentSkill"
    }
    if ($Bots -ne 2) {
        throw 'OpponentSkill requires Bots=2.'
    }
}

$normalizedSeeds = New-Object System.Collections.Generic.List[string]
foreach ($seedValue in $Seeds) {
    [UInt64] $parsedSeed = 0
    if (-not [UInt64]::TryParse($seedValue, [ref] $parsedSeed)) {
        throw "Seed must be an unsigned 64-bit integer: $seedValue"
    }
    $seed = $parsedSeed.ToString([Globalization.CultureInfo]::InvariantCulture)
    if (-not $normalizedSeeds.Contains($seed)) {
        $normalizedSeeds.Add($seed)
    }
}
if ($normalizedSeeds.Count -eq 0) {
    throw 'At least one seed is required.'
}

if ([string]::IsNullOrWhiteSpace($BotName) -or $BotName.Contains('"')) {
    throw 'BotName must not be empty or contain a double quote.'
}

$normalizedFixtureId = $FixtureId.Trim()
if ($normalizedFixtureId.Contains('"')) {
    throw 'FixtureId must not contain a double quote.'
}
$traceValidatorPath = Join-Path $PSScriptRoot 'Validate-BotTrace.py'
$pythonExecutable = $null
if ($normalizedFixtureId.Length -gt 0) {
    if (-not (Test-Path -LiteralPath $traceValidatorPath -PathType Leaf)) {
        throw "Controlled fixture trace validator was not found: $traceValidatorPath"
    }
    $pythonCommand = Get-Command python -CommandType Application -ErrorAction Stop | Select-Object -First 1
    $pythonExecutable = $pythonCommand.Source
}

$ticks = [UInt64] [Math]::Ceiling($Seconds / $FixedDelta)
if ($ticks -lt 1 -or $ticks -gt 10000000) {
    throw "Seconds and FixedDelta produce $ticks ticks; the engine accepts [1, 10000000]."
}

$timestamp = [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss', [Globalization.CultureInfo]::InvariantCulture)
$batchBaseName = "botbench-matrix-$timestamp-pid$PID"
$batchRoot = Join-Path $resolvedOutputRoot $batchBaseName
$suffix = 0
while (Test-Path -LiteralPath $batchRoot) {
    $suffix++
    $batchRoot = Join-Path $resolvedOutputRoot "$batchBaseName-$suffix"
}
New-Item -ItemType Directory -Path $batchRoot | Out-Null

$invariant = [Globalization.CultureInfo]::InvariantCulture
$fixedDeltaText = $FixedDelta.ToString('R', $invariant)
$runRows = New-Object System.Collections.Generic.List[object]
$totalRuns = $normalizedMaps.Count * $normalizedSkills.Count * $normalizedSeeds.Count * $RunsPerCase
$runNumber = 0

foreach ($map in $normalizedMaps) {
    foreach ($skill in $normalizedSkills) {
        foreach ($seed in $normalizedSeeds) {
            $skillCase = if ($OpponentSkill -ge 0) { "skill-$skill-vs-$OpponentSkill" } else { "skill-$skill" }
            $caseId = "$(ConvertTo-SafeName $map)-$skillCase-seed-$seed"
            foreach ($repetition in 1..$RunsPerCase) {
                $runNumber++
                $runId = "$caseId-run-$repetition"
                $runDirectory = Join-Path $batchRoot $runId
                New-Item -ItemType Directory -Path $runDirectory | Out-Null

                $scenario = "matrix-$caseId"
                $url = "$map`?Game=Botpack.DeathMatchPlus"
                $argumentValues = @(
                    "--botbench=$scenario",
                    "--botbench-output=$runDirectory",
                    "--botbench-url=$url",
                    "--botbench-bot-name=$BotName",
                    "--botbench-seed=$seed",
                    "--botbench-ticks=$ticks",
                    "--botbench-fixed-delta=$fixedDeltaText",
                    "--botbench-skill=$skill"
                )
                if ($OpponentSkill -ge 0) {
                    $argumentValues += "--botbench-skills=$skill,$OpponentSkill"
                }
                if ($normalizedFixtureId.Length -gt 0) {
                    $argumentValues += "--botbench-fixture=$normalizedFixtureId"
                }
                $argumentValues += "--botbench-bots=$Bots"
                $argumentValues += $resolvedGameRoot
                $argumentLine = (($argumentValues | ForEach-Object { ConvertTo-ProcessArgument $_ }) -join ' ')
                Set-Content -LiteralPath (Join-Path $runDirectory 'invocation.txt') -Value "$resolvedEnginePath $argumentLine" -Encoding UTF8

                Write-Host ("[{0}/{1}] {2}" -f $runNumber, $totalRuns, $runId)
                $wallClock = [Diagnostics.Stopwatch]::StartNew()
                $processExitCode = -1
                $processResult = 'launch_error'
                $launchError = ''

                try {
                    $process = Start-Process -FilePath $resolvedEnginePath -ArgumentList $argumentLine -PassThru -WindowStyle Hidden
                    if ($process.WaitForExit($TimeoutSeconds * 1000)) {
                        $process.Refresh()
                        $processExitCode = $process.ExitCode
                        $processResult = 'exited'
                    }
                    else {
                        $processResult = 'timeout'
                        Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
                        $process.WaitForExit()
                    }
                }
                catch {
                    $launchError = $_.Exception.Message
                }
                finally {
                    $wallClock.Stop()
                }

                $validationErrors = New-Object System.Collections.Generic.List[string]
                if ($processResult -ne 'exited') {
                    Add-ValidationError $validationErrors "Process result was $processResult. $launchError"
                }
                elseif ($processExitCode -ne 0) {
                    Add-ValidationError $validationErrors "Process exit code was $processExitCode."
                }

                $summaryPath = Join-Path $runDirectory 'summary.json'
                $eventsPath = Join-Path $runDirectory 'events.jsonl'
                $summary = $null
                if (-not (Test-Path -LiteralPath $summaryPath -PathType Leaf)) {
                    Add-ValidationError $validationErrors 'summary.json was not produced.'
                }
                else {
                    try {
                        $summary = Get-Content -LiteralPath $summaryPath -Raw | ConvertFrom-Json
                    }
                    catch {
                        Add-ValidationError $validationErrors "summary.json is invalid JSON: $($_.Exception.Message)"
                    }
                }
                if (-not (Test-Path -LiteralPath $eventsPath -PathType Leaf) -or (Get-Item -LiteralPath $eventsPath -ErrorAction SilentlyContinue).Length -le 0) {
                    Add-ValidationError $validationErrors 'events.jsonl is missing or empty.'
                }

                if ($normalizedFixtureId.Length -gt 0 -and
                    (Test-Path -LiteralPath $summaryPath -PathType Leaf) -and
                    (Test-Path -LiteralPath $eventsPath -PathType Leaf)) {
                    $traceValidationPath = Join-Path $runDirectory 'trace-validation.json'
                    try {
                        $validatorConsole = @(& $pythonExecutable $traceValidatorPath $runDirectory --output $traceValidationPath 2>&1)
                        $validatorExitCode = $LASTEXITCODE
                        if ($validatorExitCode -ne 0) {
                            $validatorDetail = ($validatorConsole | ForEach-Object { $_.ToString() }) -join ' '
                            if (Test-Path -LiteralPath $traceValidationPath -PathType Leaf) {
                                try {
                                    $traceValidation = Get-Content -LiteralPath $traceValidationPath -Raw | ConvertFrom-Json
                                    if (@($traceValidation.errors).Count -gt 0) {
                                        $validatorDetail = (@($traceValidation.errors) -join '; ')
                                    }
                                }
                                catch {
                                    # Preserve the validator console output when its report cannot be parsed.
                                }
                            }
                            Add-ValidationError $validationErrors "Controlled fixture trace validation failed: $validatorDetail"
                        }
                    }
                    catch {
                        Add-ValidationError $validationErrors "Controlled fixture trace validator could not run: $($_.Exception.Message)"
                    }
                }

                $summaryStatus = ''
                $digest = ''
                $summaryReason = ''
                $deaths = $null
                $eventCount = $null
                $maximumInventory = $null
                $observedMovement = $null
                $observedLiveBot = $null
                $finalStates = ''
				$firstNonstarterWeaponTick = $null
				$maximumWeaponCount = $null
				$maximumUsefulAmmo = $null
				$healthGainedTotal = $null
				$damageTakenProxyTotal = $null
				$damageDealtExactTotal = $null
				$damageTakenExactTotal = $null
				$selfDamageExactTotal = $null
				$fatalDamageKillsExactTotal = $null
				$fatalDamageDeathsExactTotal = $null
				$hitscanShotsTotal = $null
				$hitscanHitsTotal = $null
				$hitscanAccuracy = $null
				$projectileLaunchesTotal = $null
				$projectileHitsFinalizedTotal = $null
				$projectileMissesFinalizedTotal = $null
				$projectileFinalizedAccuracy = $null
				$firingIntentSecondsTotal = $null
				$scoreTotal = $null
				$priDeathsTotal = $null
				$noProgressSecondsTotal = $null
				$stuckEventsTotal = $null
				$candidateScore = $null
				$opponentScore = $null
				$candidateDeaths = $null
				$opponentDeaths = $null
				$candidateFirstWeaponTick = $null
				$opponentFirstWeaponTick = $null
				$candidateScoreMargin = $null
				$candidateDeathAdvantage = $null
				$candidateDamageDealtExact = $null
				$opponentDamageDealtExact = $null

                if ($null -ne $summary) {
                    $summaryStatus = [string] $summary.status
                    $summaryReason = [string] $summary.reason
                    $digest = [string] $summary.digest_fnv1a64
                    $deaths = $summary.deaths
                    $eventCount = $summary.event_count
                    $maximumInventory = $summary.maximum_observed_inventory
                    $observedMovement = $summary.observed_bot_movement
                    $observedLiveBot = $summary.observed_live_bot
                    $finalStates = (($summary.bots | ForEach-Object { "$($_.actor):$($_.state)" }) -join ';')
					$metricRows = @($summary.bot_metrics)
					if ($metricRows.Count -gt 0) {
						$novelTicks = @($metricRows | ForEach-Object { $_.first_nonstarter_weapon_tick } | Where-Object { $null -ne $_ })
						if ($novelTicks.Count -gt 0) { $firstNonstarterWeaponTick = ($novelTicks | Measure-Object -Minimum).Minimum }
						$maximumWeaponCount = ($metricRows | Measure-Object -Property maximum_weapon_count -Maximum).Maximum
						$maximumUsefulAmmo = ($metricRows | Measure-Object -Property maximum_useful_ammo -Maximum).Maximum
						$healthGainedTotal = ($metricRows | Measure-Object -Property health_gained -Sum).Sum
						$damageTakenProxyTotal = ($metricRows | Measure-Object -Property damage_taken_snapshot_proxy -Sum).Sum
						$damageDealtExactTotal = ($metricRows | Measure-Object -Property damage_dealt_exact -Sum).Sum
						$damageTakenExactTotal = ($metricRows | Measure-Object -Property damage_taken_exact -Sum).Sum
						$selfDamageExactTotal = ($metricRows | Measure-Object -Property self_damage_exact -Sum).Sum
						$fatalDamageKillsExactTotal = ($metricRows | Measure-Object -Property fatal_damage_kills_exact -Sum).Sum
						$fatalDamageDeathsExactTotal = ($metricRows | Measure-Object -Property fatal_damage_deaths_exact -Sum).Sum
						$firingIntentSecondsTotal = ($metricRows | Measure-Object -Property firing_intent_seconds -Sum).Sum
						$combatRows = @($metricRows | ForEach-Object { @($_.combat_by_weapon) })
						$hitscanShotsTotal = 0
						$hitscanHitsTotal = 0
						$projectileLaunchesTotal = 0
						$projectileHitsFinalizedTotal = 0
						$projectileMissesFinalizedTotal = 0
						if ($combatRows.Count -gt 0) {
							$hitscanShotsTotal = ($combatRows | Measure-Object -Property hitscan_shots -Sum).Sum
							$hitscanHitsTotal = ($combatRows | Measure-Object -Property hitscan_hits -Sum).Sum
							$projectileLaunchesTotal = ($combatRows | Measure-Object -Property projectile_launches -Sum).Sum
							$projectileHitsFinalizedTotal = ($combatRows | Measure-Object -Property projectile_hits_finalized -Sum).Sum
							$projectileMissesFinalizedTotal = ($combatRows | Measure-Object -Property projectile_misses_finalized -Sum).Sum
						}
						if ([double]$hitscanShotsTotal -gt 0) { $hitscanAccuracy = [double]$hitscanHitsTotal / [double]$hitscanShotsTotal }
						$projectileFinalizedTotal = [double]$projectileHitsFinalizedTotal + [double]$projectileMissesFinalizedTotal
						if ($projectileFinalizedTotal -gt 0) { $projectileFinalizedAccuracy = [double]$projectileHitsFinalizedTotal / $projectileFinalizedTotal }
						$scoreTotal = ($metricRows | Measure-Object -Property last_score -Sum).Sum
						$priDeathsTotal = ($metricRows | Measure-Object -Property maximum_pri_deaths -Sum).Sum
						$noProgressSecondsTotal = ($metricRows | Measure-Object -Property no_progress_seconds_proxy -Sum).Sum
						$stuckEventsTotal = ($metricRows | Measure-Object -Property stuck_events_proxy -Sum).Sum
						if ($OpponentSkill -ge 0 -and $metricRows.Count -eq 2) {
							$candidateScore = $metricRows[0].last_score
							$opponentScore = $metricRows[1].last_score
							$candidateDeaths = $metricRows[0].maximum_pri_deaths
							$opponentDeaths = $metricRows[1].maximum_pri_deaths
							$candidateFirstWeaponTick = $metricRows[0].first_nonstarter_weapon_tick
							$opponentFirstWeaponTick = $metricRows[1].first_nonstarter_weapon_tick
							$candidateScoreMargin = [double]$candidateScore - [double]$opponentScore
							$candidateDeathAdvantage = [double]$opponentDeaths - [double]$candidateDeaths
							$candidateDamageDealtExact = $metricRows[0].damage_dealt_exact
							$opponentDamageDealtExact = $metricRows[1].damage_dealt_exact
						}
					}

                    if ([int] $summary.schema -ne 1) { Add-ValidationError $validationErrors "Unexpected summary schema: $($summary.schema)" }
                    if ($summaryStatus -ne 'passed') { Add-ValidationError $validationErrors "Benchmark status was '$summaryStatus': $summaryReason" }
                    if ([string] $summary.seed -ne $seed) { Add-ValidationError $validationErrors "Summary seed '$($summary.seed)' did not match '$seed'." }
                    if ([string] $summary.map -ne $map) { Add-ValidationError $validationErrors "Summary map '$($summary.map)' did not match '$map'." }
                    if ([int] $summary.requested_difficulty -ne $skill) { Add-ValidationError $validationErrors 'Summary difficulty did not match.' }
                    if ([int] $summary.requested_bots -ne $Bots) { Add-ValidationError $validationErrors 'Summary bot count did not match.' }
                    if ([string] $summary.fixture_id -ne $normalizedFixtureId) { Add-ValidationError $validationErrors "Summary fixture '$($summary.fixture_id)' did not match '$normalizedFixtureId'." }
                    if ($normalizedFixtureId.Length -gt 0) {
                        if ([string] $summary.fixture.id -ne $normalizedFixtureId) { Add-ValidationError $validationErrors 'Nested summary fixture ID did not match.' }
                        if ([string] $summary.fixture.status -ne 'passed') { Add-ValidationError $validationErrors "Fixture status was '$($summary.fixture.status)'." }
                        if ([int] $summary.fixture.assertions_failed -ne 0) { Add-ValidationError $validationErrors 'Fixture summary contained failed assertions.' }
                    }
                    elseif ([string] $summary.fixture.status -ne 'inactive') {
                        Add-ValidationError $validationErrors "Ordinary run had fixture status '$($summary.fixture.status)'."
                    }
                    $expectedSkills = if ($OpponentSkill -ge 0) { "$skill,$OpponentSkill" } else { (@(1..$Bots | ForEach-Object { $skill }) -join ',') }
                    $actualSkills = (@($summary.requested_skills) -join ',')
                    if ($actualSkills -ne $expectedSkills) { Add-ValidationError $validationErrors "Summary skills '$actualSkills' did not match '$expectedSkills'." }
                    if ($normalizedFixtureId.Length -gt 0) {
                        if ([UInt64] $summary.ticks -eq 0 -or [UInt64] $summary.ticks -gt $ticks) { Add-ValidationError $validationErrors "Fixture summary ticks '$($summary.ticks)' were outside 1..$ticks." }
                    }
                    elseif ([UInt64] $summary.ticks -ne $ticks) { Add-ValidationError $validationErrors "Summary ticks '$($summary.ticks)' did not match '$ticks'." }
                    if ($digest -notmatch '^[0-9a-fA-F]{16}$') { Add-ValidationError $validationErrors "Digest is not a 64-bit hexadecimal value: '$digest'" }
                }

                $runRows.Add([PSCustomObject] [ordered] @{
                    case_id = $caseId
                    run_id = $runId
                    map = $map
                    skill = $skill
                    opponent_skill = if ($OpponentSkill -ge 0) { $OpponentSkill } else { $null }
                    seed = $seed
                    repetition = $repetition
                    requested_bots = $Bots
                    requested_ticks = $ticks
                    fixture_id = $normalizedFixtureId
                    fixture_status = if ($null -ne $summary) { [string] $summary.fixture.status } else { '' }
                    fixture_assertions_total = if ($null -ne $summary) { $summary.fixture.assertions_total } else { $null }
                    fixture_assertions_failed = if ($null -ne $summary) { $summary.fixture.assertions_failed } else { $null }
                    fixed_delta = $fixedDeltaText
                    process_result = $processResult
                    exit_code = $processExitCode
                    summary_status = $summaryStatus
                    valid = ($validationErrors.Count -eq 0)
                    deterministic = $false
                    digest_fnv1a64 = $digest
                    deaths = $deaths
                    event_count = $eventCount
                    maximum_observed_inventory = $maximumInventory
					first_nonstarter_weapon_tick_min = $firstNonstarterWeaponTick
					maximum_weapon_count_max = $maximumWeaponCount
					maximum_useful_ammo_max = $maximumUsefulAmmo
					health_gained_total = $healthGainedTotal
					damage_taken_snapshot_proxy_total = $damageTakenProxyTotal
					damage_dealt_exact_total = $damageDealtExactTotal
					damage_taken_exact_total = $damageTakenExactTotal
					self_damage_exact_total = $selfDamageExactTotal
					fatal_damage_kills_exact_total = $fatalDamageKillsExactTotal
					fatal_damage_deaths_exact_total = $fatalDamageDeathsExactTotal
					hitscan_shots_total = $hitscanShotsTotal
					hitscan_hits_total = $hitscanHitsTotal
					hitscan_accuracy = $hitscanAccuracy
					projectile_launches_total = $projectileLaunchesTotal
					projectile_hits_finalized_total = $projectileHitsFinalizedTotal
					projectile_misses_finalized_total = $projectileMissesFinalizedTotal
					projectile_finalized_accuracy = $projectileFinalizedAccuracy
					firing_intent_seconds_total = $firingIntentSecondsTotal
					final_score_total = $scoreTotal
					pri_deaths_total = $priDeathsTotal
					no_progress_seconds_proxy_total = $noProgressSecondsTotal
					stuck_events_proxy_total = $stuckEventsTotal
					candidate_score = $candidateScore
					opponent_score = $opponentScore
					candidate_deaths = $candidateDeaths
					opponent_deaths = $opponentDeaths
					candidate_first_nonstarter_weapon_tick = $candidateFirstWeaponTick
					opponent_first_nonstarter_weapon_tick = $opponentFirstWeaponTick
					candidate_score_margin = $candidateScoreMargin
					candidate_death_advantage = $candidateDeathAdvantage
					candidate_damage_dealt_exact = $candidateDamageDealtExact
					opponent_damage_dealt_exact = $opponentDamageDealtExact
                    observed_bot_movement = $observedMovement
                    observed_live_bot = $observedLiveBot
                    final_bot_states = $finalStates
                    wall_seconds = [Math]::Round($wallClock.Elapsed.TotalSeconds, 3)
                    validation_error = ($validationErrors -join ' ')
                    output_directory = $runDirectory
                })
            }
        }
    }
}

$caseRows = New-Object System.Collections.Generic.List[object]
foreach ($group in ($runRows | Group-Object case_id)) {
    $validRuns = @($group.Group | Where-Object { $_.valid })
    $digests = @($validRuns | Select-Object -ExpandProperty digest_fnv1a64 -Unique)
    $deterministic = $validRuns.Count -eq $RunsPerCase -and $digests.Count -eq 1
    foreach ($run in $group.Group) {
        $run.deterministic = $deterministic
        if (-not $deterministic -and [string]::IsNullOrWhiteSpace($run.validation_error)) {
            $run.validation_error = 'Repeated runs did not produce one identical valid digest.'
        }
    }

    $first = $group.Group | Select-Object -First 1
    $caseRows.Add([PSCustomObject] [ordered] @{
        case_id = $group.Name
        map = $first.map
        skill = $first.skill
        opponent_skill = $first.opponent_skill
        fixture_id = $first.fixture_id
        seed = $first.seed
        expected_runs = $RunsPerCase
        valid_runs = $validRuns.Count
        unique_valid_digests = $digests.Count
        digest_fnv1a64 = if ($digests.Count -eq 1) { $digests[0] } else { '' }
        deterministic = $deterministic
    })
}

$failedRuns = @($runRows | Where-Object { -not $_.valid }).Count
$failedCases = @($caseRows | Where-Object { -not $_.deterministic }).Count
$overallPassed = $failedRuns -eq 0 -and $failedCases -eq 0

$runCsvPath = Join-Path $batchRoot 'matrix-runs.csv'
$caseCsvPath = Join-Path $batchRoot 'matrix-cases.csv'
$jsonPath = Join-Path $batchRoot 'matrix-results.json'
$runRows | Export-Csv -LiteralPath $runCsvPath -NoTypeInformation -Encoding UTF8
$caseRows | Export-Csv -LiteralPath $caseCsvPath -NoTypeInformation -Encoding UTF8

$result = [PSCustomObject] [ordered] @{
    schema = 1
    created_utc = [DateTime]::UtcNow.ToString('o', $invariant)
    passed = $overallPassed
    batch_root = $batchRoot
    engine_path = $resolvedEnginePath
    game_root = $resolvedGameRoot
    configuration = [PSCustomObject] [ordered] @{
        maps = $normalizedMaps.ToArray()
        skills = @($normalizedSkills)
        seeds = $normalizedSeeds.ToArray()
        bots = $Bots
        opponent_skill = if ($OpponentSkill -ge 0) { $OpponentSkill } else { $null }
        runs_per_case = $RunsPerCase
        seconds = $Seconds
        fixed_delta = $FixedDelta
        ticks = $ticks
        timeout_seconds = $TimeoutSeconds
        bot_name = $BotName
        fixture_id = $normalizedFixtureId
    }
    totals = [PSCustomObject] [ordered] @{
        cases = $caseRows.Count
        runs = $runRows.Count
        failed_runs = $failedRuns
        nondeterministic_cases = $failedCases
    }
    cases = $caseRows.ToArray()
    runs = $runRows.ToArray()
}
$result | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $jsonPath -Encoding UTF8

Write-Host "Matrix results: $batchRoot"
Write-Host "Runs: $($runRows.Count), invalid runs: $failedRuns, nondeterministic cases: $failedCases"
if (-not $overallPassed) {
    exit 1
}
exit 0
