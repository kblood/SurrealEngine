[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string]$RetailRoot,

    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string]$OutputRoot,

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

function Invoke-BoundedRetailServer([string]$Executable, [string[]]$Arguments,
    [string]$WorkingDirectory, [string]$StandardOutput, [string]$StandardError,
    [int]$TimeoutSeconds) {
    $process = Start-Process -FilePath $Executable -ArgumentList $Arguments `
        -WorkingDirectory $WorkingDirectory -RedirectStandardOutput $StandardOutput `
        -RedirectStandardError $StandardError -PassThru
    if (!$process.WaitForExit($TimeoutSeconds * 1000)) {
        Stop-Process -Id $process.Id -Force
        $process.WaitForExit()
        return [pscustomobject]@{ exit_code = $process.ExitCode; terminated_by_runner = $true }
    }
    return [pscustomobject]@{ exit_code = $process.ExitCode; terminated_by_runner = $false }
}

$retail = (Resolve-Path -LiteralPath $RetailRoot).Path
$output = [System.IO.Path]::GetFullPath($OutputRoot)
$scriptRoot = Split-Path -Parent $PSCommandPath
$packageSource = Join-Path $scriptRoot 'RetailHitWallOracle\UT'
$ucc = Join-Path $retail 'System\UCC.exe'
$map = Join-Path $retail 'Maps\DM-Deck16][.unr'
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
    foreach ($assetDirectory in @('Maps', 'Music', 'Sounds', 'Textures')) {
        $source = Join-Path $retail $assetDirectory
        $destination = Join-Path $runtime $assetDirectory
        New-Item -ItemType Junction -Path $destination -Target $source | Out-Null
    }
    $runtimeSystem = Join-Path $runtime 'System'
    $runtimePackage = Join-Path $runtime 'RetailHitWallOracleUT'
    Copy-Item -LiteralPath $packageSource -Destination $runtimePackage -Recurse
    $packageLine = 'EditPackages=RetailHitWallOracleUT'
    foreach ($iniName in @('Default.ini', 'UnrealTournament.ini', 'Server.ini')) {
        $ini = Join-Path $runtimeSystem $iniName
        if (Test-Path -LiteralPath $ini) {
            Add-EditPackage $ini $packageLine
        }
    }

    $compileLog = Join-Path $output 'compile.stdout.log'
    $compileError = Join-Path $output 'compile.stderr.log'
    $compile = Start-Process -FilePath (Join-Path $runtimeSystem 'UCC.exe') `
        -ArgumentList @('make') -WorkingDirectory $runtimeSystem `
        -RedirectStandardOutput $compileLog -RedirectStandardError $compileError -Wait -PassThru
    if ($compile.ExitCode -ne 0) {
        throw "Retail UCC package compile failed with exit code $($compile.ExitCode)."
    }

    $runs = @()
    $port = 7890
    foreach ($threshold in $MinHitWallMilli) {
        foreach ($caseId in $Cases) {
            $runId = "case-$caseId-min-$threshold"
            $runDirectory = Join-Path $output $runId
            New-Item -ItemType Directory -Path $runDirectory | Out-Null
            $url = "DM-Deck16][?Game=RetailHitWallOracleUT.RetailHitWallOracleUTGame?OracleCase=${caseId}?OracleMinHitWallMilli=${threshold}?OracleDurationSeconds=${DurationSeconds}?FragLimit=0?TimeLimit=0?LocalLog=true?Port=${port}"
            $stdout = Join-Path $runDirectory 'server.stdout.log'
            $stderr = Join-Path $runDirectory 'server.stderr.log'
            $server = Invoke-BoundedRetailServer -Executable (Join-Path $runtimeSystem 'UCC.exe') `
                -Arguments @('server', $url, '-ini=Server.ini', "-log=$runId.log") `
                -WorkingDirectory $runtimeSystem -StandardOutput $stdout `
                -StandardError $stderr -TimeoutSeconds ($DurationSeconds + 8)
            $oracleEvents = @(Get-ChildItem -LiteralPath (Join-Path $runtime 'Logs') -File `
                -Filter '*.log' -ErrorAction SilentlyContinue |
                Select-String -Encoding Unicode -SimpleMatch -Pattern 'minhitwall_oracle')
            $hitWallEvents = @($oracleEvents | Where-Object { $_.Line -match 'hitwall_pre' })
            if (!$AllowMissingHitWall -and $hitWallEvents.Count -eq 0) {
                throw "Retail oracle emitted no HitWall event for $runId."
            }
            $runs += [pscustomobject]@{
                id = $runId
                url = $url
                exit_code = $server.exit_code
                terminated_by_runner = $server.terminated_by_runner
                stdout = [System.IO.Path]::GetRelativePath($output, $stdout)
                stderr = [System.IO.Path]::GetRelativePath($output, $stderr)
                oracle_event_count = $oracleEvents.Count
                hitwall_event_count = $hitWallEvents.Count
            }
            $port++
        }
    }
    Write-Json (Join-Path $output 'runs.json') $runs
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
