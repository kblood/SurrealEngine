[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string]$RetailRoot,

    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string]$OutputRoot,

    [ValidateSet('UT436', 'Unreal226b')]
    [string]$Profile = 'UT436',

    [ValidateSet(0, 1, 2)]
    [int[]]$Cases = @(0, 1),

    [int[]]$MinHitWallMilli = @(-500, -350),

    [int[]]$MinHitWallMicro = @(),

    [switch]$PinnedMicrothreshold,

    [ValidateRange(1, 5)]
    [int]$Repetitions = 1,

    [ValidateRange(2, 30)]
    [int]$DurationSeconds = 6,

    [switch]$AllowMissingHitWall,

    [switch]$KeepRuntime
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Get-FileInventory([string]$Root) {
    $resolvedRoot = (Resolve-Path -LiteralPath $Root).Path
    $entries = foreach ($file in Get-ChildItem -LiteralPath $resolvedRoot -File -Recurse -Force) {
        [pscustomobject]@{
            path = $file.FullName.Substring($resolvedRoot.Length).TrimStart([char[]]@('\', '/'))
            length = $file.Length
            sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $file.FullName).Hash.ToLowerInvariant()
        }
    }
    return @($entries | Sort-Object path)
}

function Write-Json([string]$Path, [object]$Value) {
    $Value | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $Path -Encoding utf8
}

function Assert-EqualInventory([object[]]$Before, [object[]]$After) {
    $beforeJson = $Before | ConvertTo-Json -Depth 6 -Compress
    $afterJson = $After | ConvertTo-Json -Depth 6 -Compress
    if ($beforeJson -ne $afterJson) {
        throw 'Installed retail file inventory changed; the isolated oracle run is invalid.'
    }
}

function Get-RetailLogInventory([string]$LogRoot) {
    if (!(Test-Path -LiteralPath $LogRoot)) {
        return @()
    }
    return @(foreach ($file in Get-ChildItem -LiteralPath $LogRoot -File -Filter '*.log' -Force) {
        [pscustomobject]@{
            path = $file.FullName
            length = $file.Length
            sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $file.FullName).Hash.ToLowerInvariant()
        }
    })
}

function Get-ChangedRetailLogs([object[]]$Before, [object[]]$After) {
    $beforeByPath = @{}
    foreach ($entry in $Before) {
        $beforeByPath[$entry.path] = $entry
    }
    $changed = [System.Collections.Generic.List[object]]::new()
    foreach ($entry in $After) {
        if (!$beforeByPath.ContainsKey($entry.path) -or
            $beforeByPath[$entry.path].length -ne $entry.length -or
            $beforeByPath[$entry.path].sha256 -ne $entry.sha256) {
            $changed.Add($entry)
        }
    }
    return @($changed)
}

function ConvertTo-OracleEvent([string]$Line) {
    $columns = $Line -split [regex]::Escape("`t"), 4
    if ($columns.Count -ne 4) {
        return $null
    }
    if ($columns[1] -ne 'minhitwall_oracle') {
        return $null
    }
    $knownEvents = @(
        'blocker_bump', 'blocker_touch', 'configuration_missing', 'handle_door_post',
        'handle_door_pre', 'hitwall_falling_return', 'hitwall_pre', 'move_begin',
        'move_return', 'mover_handle_door_enter', 'mover_handle_door_return',
        'oracle_complete', 'pick_wall_adjust_result',
        'pick_wall_adjust_skipped', 'postflight_blocker', 'preflight_blocker',
        'preflight_mover', 'pinned_contact_selected', 'pinned_direction_rejected',
        'pinned_start_rejected', 'pinned_start_selected',
        'probe_bump', 'probe_missing', 'setup_rejected', 'spawn_failed', 'start_rejected')
    if ($knownEvents -notcontains $columns[2]) {
        throw "Unrecognized retail oracle event: $($columns[2])"
    }
    $fields = @{}
    foreach ($token in $columns[3].Split(';')) {
        if ($token.Length -eq 0) {
            continue
        }
        $equals = $token.IndexOf('=')
        if ($equals -le 0 -or $equals -ne $token.LastIndexOf('=')) {
            throw "Malformed retail oracle field in '$($columns[2])': $token"
        }
        $key = $token.Substring(0, $equals)
        if ($fields.ContainsKey($key)) {
            throw "Duplicate retail oracle field in '$($columns[2])': $key"
        }
        $fields[$key] = $token.Substring($equals + 1)
    }
    if (!$fields.ContainsKey('run') -or !$fields.ContainsKey('seq')) {
        throw "Retail oracle event is missing run or sequence evidence: $($columns[2])"
    }
    return [pscustomobject]@{
        timestamp = $columns[0]
        event = $columns[2]
        fields = $fields
        raw = $Line
    }
}

function Read-OracleEvents([string]$LogPath, [string]$RunId, [bool]$UseUnicode) {
    $events = [System.Collections.Generic.List[object]]::new()
    $encoding = if ($UseUnicode) { 'Unicode' } else { 'Default' }
    foreach ($line in Get-Content -LiteralPath $LogPath -Encoding $encoding) {
        $event = ConvertTo-OracleEvent $line
        if ($null -eq $event) {
            continue
        }
        if ($event.fields.run -ne $RunId) {
            throw "Oracle log contains a foreign run id '$($event.fields.run)' (expected '$RunId')."
        }
        $events.Add($event)
    }
    return @($events)
}

function Get-OracleFloat([string]$Value, [string]$FieldName) {
    $parsed = 0.0
    if (![double]::TryParse($Value, [Globalization.NumberStyles]::Float,
        [Globalization.CultureInfo]::InvariantCulture, [ref]$parsed)) {
        throw "Retail oracle field '$FieldName' is not an invariant floating-point value: $Value"
    }
    return $parsed
}

function Assert-OracleRun([object[]]$Events, [string]$RunId, [int]$CaseId,
    [double]$ThresholdValue, [bool]$CanSuppressCallback) {
    if ($Events.Count -eq 0) {
        throw "Retail oracle emitted no tagged events for $RunId."
    }
    $lastSequence = 0
    foreach ($event in $Events) {
        $sequence = 0
        if (![int]::TryParse($event.fields.seq, [ref]$sequence) -or $sequence -le $lastSequence) {
            throw "Retail oracle event sequence is not strictly monotonic for $RunId."
        }
        $lastSequence = $sequence
    }
    $moveBegins = @($Events | Where-Object { $_.event -eq 'move_begin' })
    $preflightName = if ($CaseId -eq 2) { 'preflight_mover' } else { 'preflight_blocker' }
    $preflights = @($Events | Where-Object { $_.event -eq $preflightName })
    $completions = @($Events | Where-Object { $_.event -eq 'oracle_complete' })
    $hitWalls = @($Events | Where-Object { $_.event -eq 'hitwall_pre' })
    if ($moveBegins.Count -ne 1 -or $preflights.Count -ne 1 -or $completions.Count -ne 1) {
        throw "Retail oracle has an incomplete or duplicate structured attempt for $RunId."
    }
    $expectedMin = $ThresholdValue
    foreach ($event in @($moveBegins + $hitWalls)) {
        if (!$event.fields.ContainsKey('case') -or [int]$event.fields.case -ne $CaseId) {
            throw "Retail oracle reported the wrong case for $RunId."
        }
        if ([Math]::Abs((Get-OracleFloat $event.fields.min 'min') - $expectedMin) -ge 0.000001) {
            throw "Retail oracle reported the wrong MinHitWall for $RunId."
        }
    }
    foreach ($event in $hitWalls) {
        if (!$event.fields.ContainsKey('state') -or $event.fields.state -ne 'OracleProbe') {
            throw "Retail oracle HitWall state is invalid for $RunId."
        }
        if (!$event.fields.ContainsKey('physics') -or $event.fields.physics -ne '1') {
            throw "Retail oracle HitWall was not dispatched while walking for $RunId."
        }
    }
    if (!$CanSuppressCallback -and $hitWalls.Count -ne 1) {
        throw "Retail oracle requires exactly one HitWall event for $RunId."
    }
    if ($CaseId -eq 2) {
        $moverEnter = @($Events | Where-Object { $_.event -eq 'mover_handle_door_enter' })
        $moverReturn = @($Events | Where-Object { $_.event -eq 'mover_handle_door_return' })
        $moverTerminal = @($completions | Where-Object { $_.fields.outcome -eq 'mover_handled' })
        $skippedAdjust = @($Events | Where-Object { $_.event -eq 'pick_wall_adjust_skipped' })
        $adjustResults = @($Events | Where-Object { $_.event -eq 'pick_wall_adjust_result' })
        if ($moverEnter.Count -ne 1 -or $moverReturn.Count -ne 1 -or
            $moverTerminal.Count -ne 1 -or $skippedAdjust.Count -ne 1 -or $adjustResults.Count -ne 0) {
            throw "Retail mover oracle ordering is incomplete for $RunId."
        }
        if (!$moverReturn[0].fields.ContainsKey('handled') -or $moverReturn[0].fields.handled -ne 'True') {
            throw "Retail mover oracle did not report a handled door for $RunId."
        }
    }
    $directContactEvents = @($Events | Where-Object {
        $_.event -eq 'hitwall_pre' -or $_.event -eq 'probe_bump' -or $_.event -eq 'blocker_bump'
    })
    if ($directContactEvents.Count -eq 0) {
        throw "Retail oracle has no direct blocker-contact witness for $RunId."
    }
    if ($CaseId -ne 2) {
        $blockerBumps = @($Events | Where-Object { $_.event -eq 'blocker_bump' })
        $probeBumps = @($Events | Where-Object { $_.event -eq 'probe_bump' })
        $expectedBlocker = $preflights[0].fields.blocker
        if ($blockerBumps.Count -lt 1 -or $probeBumps.Count -lt 1 -or
            $blockerBumps.Count -lt $probeBumps.Count -or !$expectedBlocker) {
            throw "Retail oracle has no complete bilateral Bump witness stream for $RunId."
        }
        for ($index = 0; $index -lt $probeBumps.Count; $index++) {
            $blockerBump = $blockerBumps[$index]
            $probeBump = $probeBumps[$index]
            $expectedBumpIndex = $index + 1
            foreach ($bump in @($blockerBump, $probeBump)) {
                if (!$bump.fields.ContainsKey('bump_index') -or
                    [int]$bump.fields.bump_index -ne $expectedBumpIndex) {
                    throw "Retail oracle Bump indices are not paired for $RunId."
                }
                if (!$bump.fields.ContainsKey('min') -or
                    [Math]::Abs((Get-OracleFloat $bump.fields.min 'min') - $expectedMin) -ge 0.000001) {
                    throw "Retail oracle Bump did not retain the live MinHitWall for $RunId."
                }
                if (!$bump.fields.ContainsKey('bump_trace_actor') -or
                    $bump.fields.bump_trace_actor -ne $expectedBlocker -or
                    !$bump.fields.ContainsKey('bump_trace_normal') -or
                    !$bump.fields.ContainsKey('bump_trace_location')) {
                    throw "Retail oracle Bump trace witness is incomplete for $RunId."
                }
            }
            if ([int]$blockerBump.fields.seq -ge [int]$probeBump.fields.seq) {
                throw "Retail oracle Bump pair ordering is invalid for $RunId."
            }
        }
        if ($hitWalls.Count -eq 1 -and
            [int]$hitWalls[0].fields.seq -ne [int]$probeBumps[$probeBumps.Count - 1].fields.seq + 1) {
            throw "Retail oracle callback did not immediately follow its final Bump witness for $RunId."
        }
    }
    return [pscustomobject]@{
        valid = $true
        preflight_observed = $true
        direct_contact_observed = $true
        oracle_complete_observed = $true
        hitwall_event_count = $hitWalls.Count
        event_count = $Events.Count
    }
}

function Assert-PinnedMicrothresholdMatrix([object[]]$Runs, [object]$ProfileConfig,
    [int]$ExpectedRepetitions) {
    if ($Runs.Count -eq 0) {
        throw 'Pinned microthreshold matrix has no completed runs.'
    }
    $referenceSignature = $null
    foreach ($run in $Runs) {
        $events = @($run.oracle_events)
        $selectedStarts = @($events | Where-Object { $_.event -eq 'pinned_start_selected' })
        $selectedContacts = @($events | Where-Object { $_.event -eq 'pinned_contact_selected' })
        $rejected = @($events | Where-Object {
            $_.event -eq 'pinned_start_rejected' -or $_.event -eq 'pinned_direction_rejected'
        })
        $preflight = @($events | Where-Object { $_.event -eq 'preflight_blocker' })
        if ($selectedStarts.Count -ne 1 -or $selectedContacts.Count -ne 1 -or
            $rejected.Count -ne 0 -or $preflight.Count -ne 1) {
            throw "Pinned microthreshold selection evidence is incomplete for $($run.id)."
        }
        $start = $selectedStarts[0].fields
        $contact = $selectedContacts[0].fields
        $flight = $preflight[0].fields
        foreach ($field in @('playerstart', 'playerstart_location', 'direction')) {
            if (!$start.ContainsKey($field) -or !$contact.ContainsKey($field)) {
                throw "Pinned microthreshold start evidence lacks $field for $($run.id)."
            }
        }
        foreach ($field in @('candidate', 'start', 'goal', 'blocker_location', 'normal',
                'direction', 'lateral_offset', 'threshold_unit', 'threshold_micro')) {
            if (!$flight.ContainsKey($field)) {
                throw "Pinned microthreshold preflight lacks $field for $($run.id)."
            }
        }
        if ($flight.candidate -ne [string]$ProfileConfig.pinned_direction -or
            $start.direction -ne [string]$ProfileConfig.pinned_direction -or
            $contact.direction -ne $flight.direction -or $flight.threshold_unit -ne 'micro' -or
            [int]$flight.threshold_micro -ne [int]$run.threshold_micro) {
            throw "Pinned microthreshold configuration does not match the requested profile for $($run.id)."
        }
        $signature = [ordered]@{
            playerstart = $start.playerstart
            playerstart_location = $start.playerstart_location
            start = $flight.start
            goal = $flight.goal
            blocker_location = $flight.blocker_location
            normal = $flight.normal
            direction = $flight.direction
            lateral_offset = $flight.lateral_offset
        }
        if ($null -eq $referenceSignature) {
            $referenceSignature = $signature
        }
        elseif (($signature | ConvertTo-Json -Compress) -ne
                ($referenceSignature | ConvertTo-Json -Compress)) {
            throw "Pinned microthreshold contact signature changed for $($run.id)."
        }
        $thresholdEvents = @($events | Where-Object {
            $_.event -eq 'preflight_blocker' -or $_.event -eq 'move_begin' -or
            $_.event -eq 'probe_bump' -or $_.event -eq 'blocker_bump' -or
            $_.event -eq 'hitwall_pre'
        })
        foreach ($event in $thresholdEvents) {
            if (!$event.fields.ContainsKey('threshold_unit') -or
                $event.fields.threshold_unit -ne 'micro' -or
                !$event.fields.ContainsKey('threshold_micro') -or
                [int]$event.fields.threshold_micro -ne [int]$run.threshold_micro) {
                throw "Pinned microthreshold event lost its exact threshold evidence for $($run.id)."
            }
        }
        $run.pinned_contact_signature = $signature
        $run.callback_dispatched = [int]$run.hitwall_event_count -eq 1
    }

    $thresholdRows = @()
    $previousCallback = $false
    $hasSuppressed = $false
    $hasDispatched = $false
    foreach ($group in @($Runs | Group-Object threshold_micro | Sort-Object { [int]$_.Name })) {
        if ($group.Count -ne $ExpectedRepetitions) {
            throw "Pinned microthreshold $($group.Name) has $($group.Count) repetitions; expected $ExpectedRepetitions."
        }
        $outcomes = @($group.Group | ForEach-Object { [bool]$_.callback_dispatched } | Select-Object -Unique)
        if ($outcomes.Count -ne 1) {
            throw "Pinned microthreshold $($group.Name) has non-deterministic callback outcomes."
        }
        $callback = $outcomes[0]
        if ($previousCallback -and !$callback) {
            throw 'Pinned microthreshold callback outcomes are not monotonic.'
        }
        $previousCallback = $callback
        if ($callback) { $hasDispatched = $true } else { $hasSuppressed = $true }
        $thresholdRows += [ordered]@{
            threshold_micro = [int]$group.Name
            callback_dispatched = $callback
            repetitions = $group.Count
            run_ids = @($group.Group | ForEach-Object { $_.id })
        }
    }
    if (!$hasSuppressed -or !$hasDispatched) {
        throw 'Pinned microthreshold matrix must contain both suppressed and dispatched thresholds.'
    }
    $suppressed = @($thresholdRows | Where-Object { !$_.callback_dispatched })
    $dispatched = @($thresholdRows | Where-Object { $_.callback_dispatched })
    return [ordered]@{
        profile = $ProfileConfig.name
        map = $ProfileConfig.map_name
        playerstart = $referenceSignature.playerstart
        playerstart_location = $referenceSignature.playerstart_location
        direction = $referenceSignature.direction
        contact_signature = $referenceSignature
        thresholds = $thresholdRows
        highest_suppressed_micro = [int]($suppressed | Select-Object -Last 1).threshold_micro
        lowest_dispatched_micro = [int]($dispatched | Select-Object -First 1).threshold_micro
        interpretation = 'This is a reproducible callback bracket, not proof of equality comparator semantics or the raw native operand.'
    }
}

function Add-EditPackage([string]$IniPath, [string]$PackageLine) {
    $lines = [System.Collections.Generic.List[string]](Get-Content -LiteralPath $IniPath)
    if ($lines.Contains($PackageLine)) {
        return
    }
    $sectionStart = $lines.IndexOf('[Editor.EditorEngine]')
    if ($sectionStart -lt 0) {
        throw "Missing [Editor.EditorEngine] section in isolated INI: $IniPath"
    }
    $insertAt = $sectionStart + 1
    while ($insertAt -lt $lines.Count -and !$lines[$insertAt].StartsWith('[')) {
        $insertAt++
    }
    $lines.Insert($insertAt, $PackageLine)
    Set-Content -LiteralPath $IniPath -Value $lines -Encoding ascii
}

function Set-IniValue([string]$IniPath, [string]$SectionName, [string]$Key, [string]$Value) {
    $lines = [System.Collections.Generic.List[string]](Get-Content -LiteralPath $IniPath)
    $sectionHeader = "[$SectionName]"
    $sectionStart = $lines.IndexOf($sectionHeader)
    if ($sectionStart -lt 0) {
        $lines.Add('')
        $lines.Add($sectionHeader)
        $lines.Add("$Key=$Value")
        Set-Content -LiteralPath $IniPath -Value $lines -Encoding ascii
        return
    }
    $sectionEnd = $sectionStart + 1
    while ($sectionEnd -lt $lines.Count -and !$lines[$sectionEnd].StartsWith('[')) {
        $sectionEnd++
    }
    for ($index = $sectionStart + 1; $index -lt $sectionEnd; $index++) {
        if ($lines[$index].StartsWith("$Key=")) {
            $lines[$index] = "$Key=$Value"
            Set-Content -LiteralPath $IniPath -Value $lines -Encoding ascii
            return
        }
    }
    $lines.Insert($sectionEnd, "$Key=$Value")
    Set-Content -LiteralPath $IniPath -Value $lines -Encoding ascii
}

function Invoke-BoundedRetailServer([string]$Executable, [string[]]$Arguments,
    [string]$WorkingDirectory, [string]$StandardOutput, [string]$StandardError,
    [int]$TimeoutSeconds) {
    $process = Start-Process -FilePath $Executable -ArgumentList $Arguments `
        -WorkingDirectory $WorkingDirectory -RedirectStandardOutput $StandardOutput `
        -RedirectStandardError $StandardError -PassThru
    if (!$process.WaitForExit($TimeoutSeconds * 1000)) {
        Stop-Process -Id $process.Id -Force
        $process.WaitForExit()
        return [pscustomobject]@{
            process_id = $process.Id
            timeout_seconds = $TimeoutSeconds
            timed_out = $true
            terminated_by_runner = $true
            termination_method = 'stop_process_force'
            exit_code = $process.ExitCode
        }
    }
    return [pscustomobject]@{
        process_id = $process.Id
        timeout_seconds = $TimeoutSeconds
        timed_out = $false
        terminated_by_runner = $false
        termination_method = 'natural_exit'
        exit_code = $process.ExitCode
    }
}

$retail = (Resolve-Path -LiteralPath $RetailRoot).Path
$output = [System.IO.Path]::GetFullPath($OutputRoot)
$scriptRoot = Split-Path -Parent $PSCommandPath
$profileConfig = switch ($Profile) {
    'UT436' {
        [pscustomobject]@{
            name = 'UT436'
            package_directory = 'UT'
            package_name = 'RetailHitWallOracleUT'
            map_name = 'DM-Deck16]['
            game_class = 'RetailHitWallOracleUT.RetailHitWallOracleUTGame'
            ini_names = @('Default.ini', 'UnrealTournament.ini', 'Server.ini')
            server_ini = 'Server.ini'
            pinned_start_milli = @(-527732, -201715, -671894)
            pinned_direction = 1
        }
    }
    'Unreal226b' {
        [pscustomobject]@{
            name = 'Unreal226b'
            package_directory = 'Unreal'
            package_name = 'RetailHitWallOracleUnreal'
            map_name = 'DmMorbias'
            game_class = 'RetailHitWallOracleUnreal.RetailHitWallOracleUnrealGame'
            ini_names = @('Default.ini', 'Unreal.ini')
            server_ini = 'Unreal.ini'
            pinned_start_milli = @(2643, -973364, -84505)
            pinned_direction = 1
        }
    }
}
$packageSource = Join-Path $scriptRoot (Join-Path 'RetailHitWallOracle' $profileConfig.package_directory)
$ucc = Join-Path $retail 'System\UCC.exe'
$map = Join-Path $retail (Join-Path 'Maps' ($profileConfig.map_name + '.unr'))
foreach ($required in @($ucc, $map, $packageSource)) {
    if (!(Test-Path -LiteralPath $required)) {
        throw "Required oracle input is missing: $required"
    }
}
$thresholdSpecs = @()
if ($PinnedMicrothreshold) {
    if ($Cases.Count -ne 1 -or $Cases[0] -ne 1) {
        throw 'Pinned microthreshold mode requires exactly case 1 (the static glancing blocker).'
    }
    if (!$PSBoundParameters.ContainsKey('MinHitWallMicro') -or $MinHitWallMicro.Count -lt 3) {
        throw 'Pinned microthreshold mode requires at least three explicit -MinHitWallMicro values.'
    }
    if ($PSBoundParameters.ContainsKey('MinHitWallMilli')) {
        throw 'Pinned microthreshold mode does not accept -MinHitWallMilli.'
    }
    if ($Repetitions -lt 2) {
        throw 'Pinned microthreshold mode requires at least two repetitions per threshold.'
    }
    if (@($MinHitWallMicro | Select-Object -Unique).Count -ne $MinHitWallMicro.Count) {
        throw 'Pinned microthreshold values must be unique.'
    }
    foreach ($threshold in $MinHitWallMicro) {
        if ($threshold -lt -1000000 -or $threshold -gt 0) {
            throw "MinHitWall micro value must be between -1000000 and 0: $threshold"
        }
        $thresholdSpecs += [pscustomobject]@{
            value = [double]$threshold / 1000000.0
            url_name = 'OracleMinHitWallMicro'
            url_value = $threshold
            unit = 'micro'
            micro = $threshold
        }
    }
}
else {
    if ($PSBoundParameters.ContainsKey('MinHitWallMicro')) {
        throw '-MinHitWallMicro requires -PinnedMicrothreshold.'
    }
    if ($Repetitions -ne 1) {
        throw '-Repetitions requires -PinnedMicrothreshold.'
    }
    foreach ($threshold in $MinHitWallMilli) {
        if ($threshold -lt -1000 -or $threshold -gt 0) {
            throw "MinHitWall milli value must be between -1000 and 0: $threshold"
        }
        $thresholdSpecs += [pscustomobject]@{
            value = [double]$threshold / 1000.0
            url_name = 'OracleMinHitWallMilli'
            url_value = $threshold
            unit = 'milli'
            micro = $null
        }
    }
}

if (Test-Path -LiteralPath $output) {
    throw "Output directory must be new: $output"
}
New-Item -ItemType Directory -Path $output | Out-Null
$runtime = Join-Path $output '_runtime'
New-Item -ItemType Directory -Path $runtime | Out-Null

$before = Get-FileInventory $retail
Write-Json (Join-Path $output 'installed-files-before.json') $before

try {
    Copy-Item -LiteralPath (Join-Path $retail 'System') -Destination (Join-Path $runtime 'System') -Recurse
    foreach ($runtimeDirectory in @('Logs', 'Save', 'Cache')) {
        New-Item -ItemType Directory -Path (Join-Path $runtime $runtimeDirectory) | Out-Null
    }
    foreach ($assetDirectory in @('Maps', 'Music', 'Sounds', 'Textures')) {
        $source = Join-Path $retail $assetDirectory
        $destination = Join-Path $runtime $assetDirectory
        New-Item -ItemType Junction -Path $destination -Target $source | Out-Null
    }
    $runtimeSystem = Join-Path $runtime 'System'
    $runtimeLogs = Join-Path $runtime 'Logs'
    $runtimePackage = Join-Path $runtime $profileConfig.package_name
    Copy-Item -LiteralPath $packageSource -Destination $runtimePackage -Recurse
    $serverIni = Join-Path $runtimeSystem $profileConfig.server_ini
    if (!(Test-Path -LiteralPath $serverIni) -and $Profile -eq 'UT436') {
        # Current GOG UT436 installs may omit the dedicated-server INI. Seed
        # it only inside the disposable runtime so UCC receives an explicit
        # server configuration without mutating owner data.
        $serverIniTemplate = Join-Path $runtimeSystem 'UnrealTournament.ini'
        if (!(Test-Path -LiteralPath $serverIniTemplate)) {
            throw "Missing isolated UT server INI and template: $serverIni"
        }
        Copy-Item -LiteralPath $serverIniTemplate -Destination $serverIni
    }
    $packageLine = 'EditPackages=' + $profileConfig.package_name
    foreach ($iniName in $profileConfig.ini_names) {
        $ini = Join-Path $runtimeSystem $iniName
        if (Test-Path -LiteralPath $ini) {
            Add-EditPackage $ini $packageLine
        }
    }
    $runtimeLogsIniPath = $runtimeLogs.Replace('\', '/')
    if (!(Test-Path -LiteralPath $serverIni)) {
        throw "Missing isolated server INI: $serverIni"
    }
    Set-IniValue $serverIni 'Engine.StatLog' 'LocalLogDir' $runtimeLogsIniPath
    Set-IniValue $serverIni 'Engine.StatLog' 'WorldLogDir' $runtimeLogsIniPath

    $compileLog = Join-Path $output 'compile.stdout.log'
    $compileError = Join-Path $output 'compile.stderr.log'
    $compile = Start-Process -FilePath (Join-Path $runtimeSystem 'UCC.exe') `
        -ArgumentList @('make') -WorkingDirectory $runtimeSystem `
        -RedirectStandardOutput $compileLog -RedirectStandardError $compileError -Wait -PassThru
    if ($compile.ExitCode -ne 0) {
        throw "Retail UCC package compile failed with exit code $($compile.ExitCode)."
    }

    $runs = @()
    Write-Json (Join-Path $output 'runs.json') $runs
    $port = 7890
    foreach ($thresholdSpec in $thresholdSpecs) {
        foreach ($caseId in $Cases) {
            $runRepetitions = if ($PinnedMicrothreshold) { 1..$Repetitions } else { @(1) }
            foreach ($repetition in $runRepetitions) {
                if ($PinnedMicrothreshold) {
                    $runId = "case-$caseId-micro-$($thresholdSpec.url_value)-rep-$repetition-port-$port"
                }
                else {
                    $runId = "case-$caseId-min-$($thresholdSpec.url_value)-port-$port"
                }
                $runDirectory = Join-Path $output $runId
                New-Item -ItemType Directory -Path $runDirectory | Out-Null
                $url = "$($profileConfig.map_name)?Game=$($profileConfig.game_class)?OracleCase=${caseId}?$($thresholdSpec.url_name)=$($thresholdSpec.url_value)?OracleDurationSeconds=${DurationSeconds}?OracleRunId=${runId}?FragLimit=0?TimeLimit=0?LocalLog=true?Port=${port}"
                if ($PinnedMicrothreshold) {
                    $url += "?OracleUseMinHitWallMicro=1?OraclePinnedCalibration=1?OraclePinnedStartXMilli=$($profileConfig.pinned_start_milli[0])?OraclePinnedStartYMilli=$($profileConfig.pinned_start_milli[1])?OraclePinnedStartZMilli=$($profileConfig.pinned_start_milli[2])?OraclePinnedDirection=$($profileConfig.pinned_direction)"
                }
            $stdout = Join-Path $runDirectory 'server.stdout.log'
            $stderr = Join-Path $runDirectory 'server.stderr.log'
            $logsBefore = Get-RetailLogInventory (Join-Path $runtime 'Logs')
            $server = Invoke-BoundedRetailServer -Executable (Join-Path $runtimeSystem 'UCC.exe') `
                -Arguments @('server', $url, "-ini=$($profileConfig.server_ini)", "-log=$runId.log") `
                -WorkingDirectory $runtimeSystem -StandardOutput $stdout `
                -StandardError $stderr -TimeoutSeconds ($DurationSeconds + 8)
            $logsAfter = Get-RetailLogInventory (Join-Path $runtime 'Logs')
            $changedLogs = @(Get-ChangedRetailLogs $logsBefore $logsAfter)
            $oracleLogSource = $null
            $oracleLogIsUnicode = $true
            if ($changedLogs.Count -eq 1) {
                $oracleLogSource = $changedLogs[0].path
            }
            elseif ($Profile -eq 'Unreal226b' -and $changedLogs.Count -eq 0) {
                # UE1 226b does not construct LocalLog in this UCC server
                # mode. Script Log() records are synchronously mirrored to
                # this per-child redirected stream, unlike its buffered file.
                $fallbackLog = $stdout
                if (Test-Path -LiteralPath $fallbackLog -PathType Leaf) {
                    $oracleLogSource = $fallbackLog
                    $oracleLogIsUnicode = $false
                }
            }
            if ($null -eq $oracleLogSource) {
                throw "Expected exactly one changed retail local log for $runId; found $($changedLogs.Count)."
            }
            $oracleLog = Join-Path $runDirectory 'oracle.local.log'
            Copy-Item -LiteralPath $oracleLogSource -Destination $oracleLog
            $oracleEvents = @(Read-OracleEvents $oracleLog $runId $oracleLogIsUnicode)
            $runRecord = [ordered]@{
                id = $runId
                profile = $Profile
                selection_mode = if ($PinnedMicrothreshold) { 'pinned_microthreshold' } else { 'legacy' }
                threshold_unit = $thresholdSpec.unit
                threshold_micro = $thresholdSpec.micro
                repetition = $repetition
                pinned_contact_signature = $null
                callback_dispatched = $null
                url = $url
                exit_code = $server.exit_code
                process_id = $server.process_id
                timeout_seconds = $server.timeout_seconds
                timed_out = $server.timed_out
                terminated_by_runner = $server.terminated_by_runner
                termination_method = $server.termination_method
                stdout = [System.IO.Path]::GetRelativePath($output, $stdout)
                stderr = [System.IO.Path]::GetRelativePath($output, $stderr)
                oracle_log = [System.IO.Path]::GetRelativePath($output, $oracleLog)
                oracle_log_encoding = if ($oracleLogIsUnicode) { 'utf-16le' } else { 'system-log' }
                oracle_log_length = (Get-Item -LiteralPath $oracleLog).Length
                oracle_log_sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $oracleLog).Hash.ToLowerInvariant()
                oracle_event_count = $oracleEvents.Count
                oracle_events = @($oracleEvents | ForEach-Object {
                    [ordered]@{ sequence = $_.fields.seq; event = $_.event; fields = $_.fields }
                })
            }
            Write-Json (Join-Path $runDirectory 'run.json') $runRecord
            $validation = Assert-OracleRun $oracleEvents $runId $caseId $thresholdSpec.value ($AllowMissingHitWall.IsPresent -or $PinnedMicrothreshold)
            foreach ($property in $validation.PSObject.Properties) {
                $runRecord[$property.Name] = $property.Value
            }
            Write-Json (Join-Path $runDirectory 'run.json') $runRecord
            $runs += [pscustomobject]$runRecord
            Write-Json (Join-Path $output 'runs.json') $runs
            $port++
            }
        }
    }
    if (@($runs | Where-Object { !$_.terminated_by_runner -and $_.exit_code -ne 0 }).Count -ne 0) {
        throw 'At least one retail oracle server case failed.'
    }
    if ($PinnedMicrothreshold) {
        $boundarySummary = Assert-PinnedMicrothresholdMatrix $runs $profileConfig $Repetitions
        Write-Json (Join-Path $output 'pinned-boundary-summary.json') $boundarySummary
        foreach ($run in $runs) {
            Write-Json (Join-Path (Join-Path $output $run.id) 'run.json') $run
        }
        Write-Json (Join-Path $output 'runs.json') $runs
    }
}
finally {
    $after = Get-FileInventory $retail
    Write-Json (Join-Path $output 'installed-files-after.json') $after
    Assert-EqualInventory $before $after
    if (!$KeepRuntime -and (Test-Path -LiteralPath $runtime)) {
        Remove-Item -LiteralPath $runtime -Recurse -Force
    }
}
