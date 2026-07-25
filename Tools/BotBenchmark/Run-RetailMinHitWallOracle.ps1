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

    [ValidateSet(0, 1)]
    [int[]]$Cases = @(0, 1),

    [int[]]$MinHitWallMilli = @(-500, -350),

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
        'move_return', 'oracle_complete', 'pick_wall_adjust_result',
        'pick_wall_adjust_skipped', 'postflight_blocker', 'preflight_blocker',
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
    [int]$ThresholdMilli, [bool]$CanSuppressCallback) {
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
    $preflights = @($Events | Where-Object { $_.event -eq 'preflight_blocker' })
    $completions = @($Events | Where-Object { $_.event -eq 'oracle_complete' })
    $hitWalls = @($Events | Where-Object { $_.event -eq 'hitwall_pre' })
    if ($moveBegins.Count -ne 1 -or $preflights.Count -ne 1 -or $completions.Count -ne 1) {
        throw "Retail oracle has an incomplete or duplicate structured attempt for $RunId."
    }
    $expectedMin = $ThresholdMilli / 1000.0
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
    $directContactEvents = @($Events | Where-Object {
        $_.event -eq 'hitwall_pre' -or $_.event -eq 'probe_bump' -or $_.event -eq 'blocker_bump'
    })
    if ($directContactEvents.Count -eq 0) {
        throw "Retail oracle has no direct blocker-contact witness for $RunId."
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
            package_directory = 'UT'
            package_name = 'RetailHitWallOracleUT'
            map_name = 'DM-Deck16]['
            game_class = 'RetailHitWallOracleUT.RetailHitWallOracleUTGame'
            ini_names = @('Default.ini', 'UnrealTournament.ini', 'Server.ini')
            server_ini = 'Server.ini'
        }
    }
    'Unreal226b' {
        [pscustomobject]@{
            package_directory = 'Unreal'
            package_name = 'RetailHitWallOracleUnreal'
            map_name = 'DmMorbias'
            game_class = 'RetailHitWallOracleUnreal.RetailHitWallOracleUnrealGame'
            ini_names = @('Default.ini', 'Unreal.ini')
            server_ini = 'Unreal.ini'
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
foreach ($threshold in $MinHitWallMilli) {
    if ($threshold -lt -1000 -or $threshold -gt 0) {
        throw "MinHitWall milli value must be between -1000 and 0: $threshold"
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
    $packageLine = 'EditPackages=' + $profileConfig.package_name
    foreach ($iniName in $profileConfig.ini_names) {
        $ini = Join-Path $runtimeSystem $iniName
        if (Test-Path -LiteralPath $ini) {
            Add-EditPackage $ini $packageLine
        }
    }
    $runtimeLogsIniPath = $runtimeLogs.Replace('\', '/')
    $serverIni = Join-Path $runtimeSystem $profileConfig.server_ini
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
    foreach ($threshold in $MinHitWallMilli) {
        foreach ($caseId in $Cases) {
            $runId = "case-$caseId-min-$threshold-port-$port"
            $runDirectory = Join-Path $output $runId
            New-Item -ItemType Directory -Path $runDirectory | Out-Null
            $url = "$($profileConfig.map_name)?Game=$($profileConfig.game_class)?OracleCase=${caseId}?OracleMinHitWallMilli=${threshold}?OracleDurationSeconds=${DurationSeconds}?OracleRunId=${runId}?FragLimit=0?TimeLimit=0?LocalLog=true?Port=${port}"
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
            $validation = Assert-OracleRun $oracleEvents $runId $caseId $threshold $AllowMissingHitWall.IsPresent
            foreach ($property in $validation.PSObject.Properties) {
                $runRecord[$property.Name] = $property.Value
            }
            Write-Json (Join-Path $runDirectory 'run.json') $runRecord
            $runs += [pscustomobject]$runRecord
            Write-Json (Join-Path $output 'runs.json') $runs
            $port++
        }
    }
    if (@($runs | Where-Object { !$_.terminated_by_runner -and $_.exit_code -ne 0 }).Count -ne 0) {
        throw 'At least one retail oracle server case failed.'
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
