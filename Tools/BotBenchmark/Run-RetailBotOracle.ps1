[CmdletBinding()]
param(
    [string]$GameRoot = 'C:\Program Files (x86)\GOG Galaxy\Games\Unreal Tournament GOTY',
    [string]$OutputRoot = (Join-Path $PWD 'retail-bot-oracle'),
    [string]$Map = 'DM-Morbias][',
    [ValidateRange(0, 7)][int]$Skill = 7,
    [ValidateRange(2, 16)][int]$Bots = 2,
    [ValidateRange(5, 3600)][int]$Seconds = 60,
    [ValidateRange(1024, 65535)][int]$Port = 7799,
    [switch]$TouchProbe,
    [switch]$KeepRuntime
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Get-ShortPath([string]$Path, [bool]$IsDirectory = $false) {
    $fso = New-Object -ComObject Scripting.FileSystemObject
    if ($IsDirectory) { return $fso.GetFolder($Path).ShortPath }
    return $fso.GetFile($Path).ShortPath
}

function Get-RetailMutableManifest([string]$SystemDirectory) {
    $importantConfigs = @('Default.ini', 'DefUser.ini', 'UnrealTournament.ini', 'User.ini')
    $records = [Collections.Generic.List[object]]::new()
    foreach ($name in $importantConfigs) {
        $path = Join-Path $SystemDirectory $name
        if (Test-Path -LiteralPath $path) {
            $item = Get-Item -LiteralPath $path
            $records.Add([ordered]@{
                name = $name
                length = $item.Length
                modified_utc = $item.LastWriteTimeUtc.ToString('o')
                sha256 = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash
            })
        }
    }
    foreach ($item in Get-ChildItem -LiteralPath $SystemDirectory -File | Where-Object Extension -In '.log', '.tmp' | Sort-Object Name) {
        $records.Add([ordered]@{
            name = $item.Name
            length = $item.Length
            modified_utc = $item.LastWriteTimeUtc.ToString('o')
            sha256 = (Get-FileHash -LiteralPath $item.FullName -Algorithm SHA256).Hash
        })
    }
    return @($records.ToArray())
}

function Add-EditPackage([string]$IniPath, [string]$PackageName) {
    $lines = [Collections.Generic.List[string]]::new()
    foreach ($line in Get-Content -LiteralPath $IniPath) { $lines.Add($line) }
    $section = $lines.IndexOf('[Editor.EditorEngine]')
    if ($section -lt 0) { throw "Missing [Editor.EditorEngine] in $IniPath" }
    $insertAt = $lines.Count
    for ($i = $section + 1; $i -lt $lines.Count; $i++) {
        if ($lines[$i] -match '^\[') { $insertAt = $i; break }
    }
    for ($i = $section + 1; $i -lt $insertAt; $i++) {
        if ($lines[$i] -eq "EditPackages=$PackageName") { return }
    }
    $lines.Insert($insertAt, "EditPackages=$PackageName")
    Set-Content -LiteralPath $IniPath -Value $lines -Encoding ASCII
}

function Start-CapturedProcess([string]$Executable, [string]$WorkingDirectory, [string[]]$Arguments) {
    $startInfo = [Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $Executable
    $startInfo.WorkingDirectory = $WorkingDirectory
    $startInfo.UseShellExecute = $false
    $startInfo.CreateNoWindow = $true
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true
    foreach ($argument in $Arguments) { [void]$startInfo.ArgumentList.Add($argument) }
    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $startInfo
    [void]$process.Start()
    return [pscustomobject]@{
        Process = $process
        Stdout = $process.StandardOutput.ReadToEndAsync()
        Stderr = $process.StandardError.ReadToEndAsync()
    }
}

function Read-NgStats([string]$Path, [int]$RequestedBots, [int]$RequestedSkill, [int]$RequestedSeconds) {
    $players = @{}
    $killCount = 0
    $suicideCount = 0
    $firstKillSeconds = $null
    $gameEndSeconds = $null
    $gameEndReason = $null
    $snapshotCount = 0
    $weaponKills = @{}
    $itemPickups = @{}
    $itemPickupCount = 0
    $touchProbe = $null

    function Ensure-Player([string]$Id) {
        if (-not $players.ContainsKey($Id)) {
            $players[$Id] = [ordered]@{
                id = [int]$Id
                name = $null
                observed_internal_skill = $null
                observed_novice = $null
                kills = 0
                deaths = 0
                suicides = 0
                item_pickups = 0
                first_item_seconds = $null
                snapshots = 0
                baseline_weapon_count = $null
                first_extra_weapon_seconds = $null
                max_inventory_count = 0
                max_weapon_count = 0
                max_ammo_total = 0
                max_health = 0
                max_armor_total = 0
            }
        }
        return $players[$Id]
    }

    $text = [IO.File]::ReadAllText($Path, [Text.Encoding]::Unicode)
    foreach ($line in $text -split "`r?`n") {
        if ([string]::IsNullOrWhiteSpace($line)) { continue }
        $field = $line -split "`t"
        if ($field.Count -lt 2) { continue }
        $time = 0.0
        [void][double]::TryParse($field[0], [Globalization.NumberStyles]::Float, [Globalization.CultureInfo]::InvariantCulture, [ref]$time)

        if ($field[1] -eq 'player' -and $field.Count -ge 5) {
            $kind = $field[2]
            if ($kind -eq 'Rename') { (Ensure-Player $field[4]).name = $field[3] }
            elseif ($kind -eq 'Skill') { (Ensure-Player $field[3]).observed_internal_skill = [double]::Parse($field[4], [Globalization.CultureInfo]::InvariantCulture) }
            elseif ($kind -eq 'Novice') { (Ensure-Player $field[3]).observed_novice = [bool]::Parse($field[4]) }
        }
        elseif ($field[1] -eq 'kill' -and $field.Count -ge 7) {
            $killer = Ensure-Player $field[2]
            $victim = Ensure-Player $field[4]
            $killer.kills++
            $victim.deaths++
            $killCount++
            if ($null -eq $firstKillSeconds) { $firstKillSeconds = $time }
            $weapon = $field[3]
            if (-not $weaponKills.ContainsKey($weapon)) { $weaponKills[$weapon] = 0 }
            $weaponKills[$weapon]++
        }
        elseif ($field[1] -eq 'suicide' -and $field.Count -ge 4) {
            $player = Ensure-Player $field[2]
            $player.suicides++
            $player.deaths++
            $suicideCount++
        }
        elseif ($field[1] -eq 'item_get' -and $field.Count -ge 4) {
            $player = Ensure-Player $field[3]
            $player.item_pickups++
            if ($null -eq $player.first_item_seconds) { $player.first_item_seconds = $time }
            $itemName = $field[2]
            if (-not $itemPickups.ContainsKey($itemName)) { $itemPickups[$itemName] = 0 }
            $itemPickups[$itemName]++
            $itemPickupCount++
        }
        elseif ($field[1] -eq 'oracle_snapshot' -and $field.Count -ge 11) {
            $player = Ensure-Player $field[2]
            $player.snapshots++
            $snapshotCount++
            $inventoryCount = [int]$field[4]
            $weaponCount = [int]$field[5]
            $ammoTotal = [int]$field[6]
            $armorTotal = [int]$field[7]
            $health = [int]$field[3]
            if ($null -eq $player.baseline_weapon_count) { $player.baseline_weapon_count = $weaponCount }
            elseif ($null -eq $player.first_extra_weapon_seconds -and $weaponCount -gt $player.baseline_weapon_count) { $player.first_extra_weapon_seconds = $time }
            $player.max_inventory_count = [Math]::Max($player.max_inventory_count, $inventoryCount)
            $player.max_weapon_count = [Math]::Max($player.max_weapon_count, $weaponCount)
            $player.max_ammo_total = [Math]::Max($player.max_ammo_total, $ammoTotal)
            $player.max_health = [Math]::Max($player.max_health, $health)
            $player.max_armor_total = [Math]::Max($player.max_armor_total, $armorTotal)
        }
        elseif ($field[1] -eq 'oracle_touch_probe' -and $field.Count -ge 6) {
            $touchProbe = [ordered]@{
                health_after = [int]$field[2]
                vials_spawned = [int]$field[3]
                touching_count = [int]$field[4]
                touching_states = $field[5]
            }
        }
        elseif ($field[1] -eq 'game_end' -and $field.Count -ge 3) {
            $gameEndSeconds = $time
            $gameEndReason = $field[2]
        }
    }

    $botRows = @($players.Values | Sort-Object id | ForEach-Object { [pscustomobject]$_ })
    $duration = if ($null -ne $gameEndSeconds -and $gameEndSeconds -gt 0) { $gameEndSeconds } else { [double]$RequestedSeconds }
    return [ordered]@{
        schema_version = 1
        engine = 'Unreal Tournament retail UCC'
        engine_version = 436
        requested_bots = $RequestedBots
        requested_skill = $RequestedSkill
        requested_duration_seconds = $RequestedSeconds
        observed_bots = $botRows.Count
        observed_duration_seconds = $gameEndSeconds
        game_end_reason = $gameEndReason
        kills = $killCount
        suicides = $suicideCount
        item_pickups = $itemPickupCount
        item_pickups_per_minute = if ($duration -gt 0) { [Math]::Round(60.0 * $itemPickupCount / $duration, 4) } else { 0 }
        item_pickups_by_name = [ordered]@{} + $itemPickups
        first_kill_seconds = $firstKillSeconds
        kills_per_minute = if ($duration -gt 0) { [Math]::Round(60.0 * $killCount / $duration, 4) } else { 0 }
        snapshot_count = $snapshotCount
        weapon_kills = [ordered]@{} + $weaponKills
        touch_probe = $touchProbe
        bots = $botRows
        valid = ($botRows.Count -eq $RequestedBots -and $snapshotCount -gt 0 -and $gameEndReason -eq 'oracle_timeout')
        limitations = @(
            'Retail RNG has no supported seed control; compare distributions across repeated runs, not event-for-event traces.',
            'One-second read-only snapshots bound pickup timing to a one-second interval.',
            'ngStats exposes kills, suicides, weapons, effective skill, novice mode, and timestamps; it does not expose perception or path-search internals.'
        )
    }
}

$gameRootPath = (Resolve-Path -LiteralPath $GameRoot).Path
$sourceSystem = Join-Path $gameRootPath 'System'
$sourceUcc = Join-Path $sourceSystem 'UCC.exe'
if (-not (Test-Path -LiteralPath $sourceUcc)) { throw "Retail UCC.exe not found under $sourceSystem" }

$outputRootPath = [IO.Path]::GetFullPath($OutputRoot)
New-Item -ItemType Directory -Force -Path $outputRootPath | Out-Null
$runId = 'retail-oracle-{0}-pid{1}' -f (Get-Date -Format 'yyyyMMdd-HHmmss'), $PID
$runDirectory = Join-Path $outputRootPath $runId
$runtimeRoot = Join-Path $runDirectory '_runtime'
$runtimeSystem = Join-Path $runtimeRoot 'System'
$runtimeStats = Join-Path $runtimeRoot 'Stats'
New-Item -ItemType Directory -Force -Path $runDirectory, $runtimeSystem, $runtimeStats | Out-Null

$manifestBefore = Get-RetailMutableManifest $sourceSystem

# Mirror read-only retail runtime files with hard links. Configs are private copies;
# retail logs and temporary files are deliberately excluded.
foreach ($source in Get-ChildItem -LiteralPath $sourceSystem -File) {
    if ($source.Extension -in '.log', '.tmp') { continue }
    $destination = Join-Path $runtimeSystem $source.Name
    if ($source.Extension -eq '.ini') {
        Copy-Item -LiteralPath $source.FullName -Destination $destination
    }
    else {
        try { New-Item -ItemType HardLink -Path $destination -Target $source.FullName | Out-Null }
        catch { Copy-Item -LiteralPath $source.FullName -Destination $destination }
    }
}
foreach ($assetDirectory in 'Maps', 'Textures', 'Sounds', 'Music') {
    $target = Join-Path $gameRootPath $assetDirectory
    if (Test-Path -LiteralPath $target) {
        New-Item -ItemType Junction -Path (Join-Path $runtimeRoot $assetDirectory) -Target $target | Out-Null
    }
}

$sourceClasses = Join-Path $PSScriptRoot 'RetailBotOracle\Classes'
$runtimeClasses = Join-Path $runtimeRoot 'RetailBotOracle\Classes'
New-Item -ItemType Directory -Force -Path $runtimeClasses | Out-Null
Copy-Item -Path (Join-Path $sourceClasses '*.uc') -Destination $runtimeClasses

Copy-Item -LiteralPath (Join-Path $runtimeSystem 'UnrealTournament.ini') -Destination (Join-Path $runtimeSystem 'Server.ini')
Copy-Item -LiteralPath (Join-Path $runtimeSystem 'User.ini') -Destination (Join-Path $runtimeSystem 'ServerUser.ini')
$serverIniPath = Join-Path $runtimeSystem 'Server.ini'
$serverIni = Get-Content -Raw -LiteralPath $serverIniPath
$serverIni = $serverIni -replace '(?m)^ServerActors=.*\r?\n', ''
$statsShortPath = (Get-ShortPath $runtimeStats $true).Replace('\', '/')
$serverIni += "`r`n[Engine.StatLog]`r`nLocalLogDir=$statsShortPath`r`nWorldLogDir=$statsShortPath`r`n"
Set-Content -LiteralPath $serverIniPath -Value $serverIni -Encoding ASCII

$makeIniPath = Join-Path $runtimeSystem 'Make.ini'
Copy-Item -LiteralPath $serverIniPath -Destination $makeIniPath
Add-EditPackage $makeIniPath 'RetailBotOracle'

$runtimeUcc = Get-ShortPath (Join-Path $runtimeSystem 'UCC.exe')
$compile = Start-CapturedProcess $runtimeUcc $runtimeSystem @('make', 'ini=Make.ini', 'userini=ServerUser.ini', '-silent')
if (-not $compile.Process.WaitForExit(30000)) {
    $compile.Process.Kill()
    throw 'RetailBotOracle UnrealScript compilation timed out.'
}
$compileStdout = $compile.Stdout.Result
$compileStderr = $compile.Stderr.Result
[IO.File]::WriteAllText((Join-Path $runDirectory 'compile.stdout.log'), $compileStdout, [Text.UTF8Encoding]::new($false))
[IO.File]::WriteAllText((Join-Path $runDirectory 'compile.stderr.log'), $compileStderr, [Text.UTF8Encoding]::new($false))
if ($compile.Process.ExitCode -ne 0 -or -not (Test-Path -LiteralPath (Join-Path $runtimeSystem 'RetailBotOracle.u'))) {
    throw "RetailBotOracle compilation failed (exit $($compile.Process.ExitCode)). See compile logs in $runDirectory"
}

$touchProbeValue = if ($TouchProbe) { 1 } else { 0 }
$url = '{0}?Game=RetailBotOracle.RetailBotOracleGame?OracleBots={1}?OracleSkill={2}?OracleDurationSeconds={3}?OracleTouchProbe={4}?FragLimit=0?TimeLimit=0?LocalLog=true' -f $Map, $Bots, $Skill, $Seconds, $touchProbeValue
$server = Start-CapturedProcess $runtimeUcc $runtimeSystem @('server', $url, "port=$Port", 'ini=Server.ini', 'userini=ServerUser.ini', 'log=RetailOracleServer.log', '-nohomedir')
$deadline = (Get-Date).AddSeconds($Seconds + 30)
$ngStatsLog = $null
do {
    Start-Sleep -Milliseconds 200
    $ngStatsLog = Get-ChildItem -LiteralPath $runtimeStats -Filter "*.${Port}.log" -ErrorAction SilentlyContinue | Sort-Object LastWriteTimeUtc -Descending | Select-Object -First 1
} while ($null -eq $ngStatsLog -and -not $server.Process.HasExited -and (Get-Date) -lt $deadline)

if (-not $server.Process.HasExited) {
    $server.Process.Kill()
    $server.Process.WaitForExit()
}
$serverStdout = $server.Stdout.Result
$serverStderr = $server.Stderr.Result
[IO.File]::WriteAllText((Join-Path $runDirectory 'server.stdout.log'), $serverStdout, [Text.UTF8Encoding]::new($false))
[IO.File]::WriteAllText((Join-Path $runDirectory 'server.stderr.log'), $serverStderr, [Text.UTF8Encoding]::new($false))

if ($null -eq $ngStatsLog) {
    throw "Retail oracle did not produce a completed ngStats log before the timeout. See $runDirectory"
}
$rawLog = Join-Path $runDirectory 'retail-ngstats.log'
Copy-Item -LiteralPath $ngStatsLog.FullName -Destination $rawLog
$summary = Read-NgStats $rawLog $Bots $Skill $Seconds
$summary['map'] = $Map
$summary['port'] = $Port
$summary['url'] = $url
$summary['retail_root'] = $gameRootPath

$manifestAfter = Get-RetailMutableManifest $sourceSystem
$manifestBeforeJson = ConvertTo-Json $manifestBefore -Depth 5 -Compress
$manifestAfterJson = ConvertTo-Json $manifestAfter -Depth 5 -Compress
$summary['installed_files_unchanged'] = ($manifestBeforeJson -ceq $manifestAfterJson)
$summary | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath (Join-Path $runDirectory 'summary.json') -Encoding UTF8
$summary.bots | Export-Csv -LiteralPath (Join-Path $runDirectory 'bot-metrics.csv') -NoTypeInformation
[ordered]@{ before = $manifestBefore; after = $manifestAfter } | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $runDirectory 'installed-file-manifest.json') -Encoding UTF8

if (-not $KeepRuntime) {
    $runFull = [IO.Path]::GetFullPath($runDirectory).TrimEnd('\') + '\'
    $runtimeFull = [IO.Path]::GetFullPath($runtimeRoot).TrimEnd('\')
    if (-not $runtimeFull.StartsWith($runFull, [StringComparison]::OrdinalIgnoreCase) -or (Split-Path $runtimeFull -Leaf) -ne '_runtime') {
        throw "Refusing to clean unexpected runtime path: $runtimeFull"
    }
    foreach ($assetDirectory in 'Maps', 'Textures', 'Sounds', 'Music') {
        $junction = Join-Path $runtimeFull $assetDirectory
        if (Test-Path -LiteralPath $junction) { Remove-Item -LiteralPath $junction -Force }
    }
    Remove-Item -LiteralPath $runtimeFull -Recurse -Force
}

Write-Host "Retail oracle complete: $runDirectory"
$firstKillDisplay = if ($null -eq $summary.first_kill_seconds) { 'n/a' } else { "$($summary.first_kill_seconds)s" }
Write-Host "Bots=$($summary.observed_bots) kills=$($summary.kills) first_kill=$firstKillDisplay snapshots=$($summary.snapshot_count) valid=$($summary.valid) installed_unchanged=$($summary.installed_files_unchanged)"
if (-not $summary.valid -or -not $summary.installed_files_unchanged) { exit 2 }
