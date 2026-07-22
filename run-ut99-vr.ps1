[CmdletBinding()]
param(
    [string]$GamePath,
    [string]$Map = 'DM-Deck16][',
    [switch]$LeftHanded,
    [switch]$WeaponTuning,
    [switch]$QuadMenu,
    [switch]$ProjectionMenu,
    [switch]$NoMenuLaser,
    [switch]$NoMenuControllers,
    [switch]$FindOnly
)

$ErrorActionPreference = 'Stop'

function Test-UT99Root([string]$Path) {
    if ([string]::IsNullOrWhiteSpace($Path)) {
        return $false
    }
    return Test-Path -LiteralPath (Join-Path $Path 'System\UnrealTournament.exe') -PathType Leaf
}

$candidatePaths = [System.Collections.Generic.List[object]]::new()
function Add-UT99Candidate([string]$Path, [string]$Source) {
    if ([string]::IsNullOrWhiteSpace($Path)) {
        return
    }
    $cleanPath = $Path.Trim().Trim('"').TrimEnd('\')
    if (-not ($candidatePaths | Where-Object { $_.Path -ieq $cleanPath })) {
        $candidatePaths.Add([pscustomobject]@{ Path = $cleanPath; Source = $Source })
    }
}

function Add-UninstallRegistryCandidates {
    $uninstallRoots = @(
        'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\*',
        'HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\*',
        'HKCU:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\*'
    )
    foreach ($root in $uninstallRoots) {
        Get-ItemProperty $root -ErrorAction SilentlyContinue |
            Where-Object { $_.DisplayName -match '^Unreal Tournament(?: GOTY)?(?: Edition)?$' } |
            ForEach-Object { Add-UT99Candidate $_.InstallLocation "Windows registry ($($_.DisplayName))" }
    }
}

function Add-SteamCandidates {
    $steamRoots = [System.Collections.Generic.List[string]]::new()
    $steamRegistryKeys = @(
        'HKCU:\SOFTWARE\Valve\Steam',
        'HKLM:\SOFTWARE\WOW6432Node\Valve\Steam'
    )
    foreach ($key in $steamRegistryKeys) {
        $steam = Get-ItemProperty $key -ErrorAction SilentlyContinue
        $steamPath = if ($steam.SteamPath) { $steam.SteamPath } else { $steam.InstallPath }
        if ($steamPath -and -not ($steamRoots -contains $steamPath)) {
            $steamRoots.Add($steamPath)
        }
    }
    Add-UT99Candidate (Join-Path ${env:ProgramFiles(x86)} 'Steam\steamapps\common\Unreal Tournament') 'default Steam location'

    $libraries = [System.Collections.Generic.List[string]]::new()
    foreach ($steamRoot in $steamRoots) {
        if (-not ($libraries -contains $steamRoot)) {
            $libraries.Add($steamRoot)
        }
        $libraryFile = Join-Path $steamRoot 'steamapps\libraryfolders.vdf'
        if (Test-Path -LiteralPath $libraryFile -PathType Leaf) {
            $vdf = Get-Content -LiteralPath $libraryFile -Raw
            foreach ($match in [regex]::Matches($vdf, '"path"\s+"([^"]+)"')) {
                $libraryPath = $match.Groups[1].Value -replace '\\\\', '\'
                if ($libraryPath -and -not ($libraries -contains $libraryPath)) {
                    $libraries.Add($libraryPath)
                }
            }
        }
    }

    foreach ($library in $libraries) {
        $manifest = Join-Path $library 'steamapps\appmanifest_13240.acf'
        if (Test-Path -LiteralPath $manifest -PathType Leaf) {
            $acf = Get-Content -LiteralPath $manifest -Raw
            $installMatch = [regex]::Match($acf, '"installdir"\s+"([^"]+)"')
            if ($installMatch.Success) {
                Add-UT99Candidate (Join-Path $library (Join-Path 'steamapps\common' $installMatch.Groups[1].Value)) 'Steam library manifest'
            }
        }
        Add-UT99Candidate (Join-Path $library 'steamapps\common\Unreal Tournament') 'Steam library'
        Add-UT99Candidate (Join-Path $library 'steamapps\common\Unreal Tournament GOTY') 'Steam library'
    }
}

if ($GamePath) {
    if (-not (Test-UT99Root $GamePath)) {
        throw "The supplied UT99 folder is not valid: $GamePath`nExpected System\UnrealTournament.exe below that folder."
    }
    $detectedGame = [pscustomobject]@{ Path = $GamePath.TrimEnd('\'); Source = '-GamePath' }
}
else {
    Add-UninstallRegistryCandidates
    Add-SteamCandidates
    Add-UT99Candidate (Join-Path ${env:ProgramFiles(x86)} 'GOG Galaxy\Games\Unreal Tournament GOTY') 'default GOG location'
    Add-UT99Candidate (Join-Path $env:ProgramFiles 'GOG Galaxy\Games\Unreal Tournament GOTY') 'default GOG location'
    Add-UT99Candidate 'C:\UnrealTournament' 'classic installer location'

    $detectedGame = $candidatePaths | Where-Object { Test-UT99Root $_.Path } | Select-Object -First 1
    if (-not $detectedGame) {
        throw @"
Could not find an Unreal Tournament installation automatically.

Install the original Unreal Tournament GOTY, or launch with its folder:
  .\run-ut99-vr.ps1 -GamePath 'D:\Games\Unreal Tournament'

The selected folder must contain System\UnrealTournament.exe.
"@
    }
}

$engineCandidates = @(
    (Join-Path $PSScriptRoot 'SurrealEngine.exe'),
    (Join-Path $PSScriptRoot 'build\Release\SurrealEngine.exe')
)
$engineExe = $engineCandidates | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -First 1
if (-not $engineExe) {
    throw "UT99 VR executable not found beside the launcher or in build\Release."
}

$GamePath = (Resolve-Path -LiteralPath $detectedGame.Path).Path
$engineExe = (Resolve-Path -LiteralPath $engineExe).Path
Write-Host "UT99 installation: $GamePath"
Write-Host "Detected via:       $($detectedGame.Source)"
Write-Host "UT99 VR executable: $engineExe"

if ($FindOnly) {
    Write-Host 'Detection test passed; the game was not launched.'
    exit 0
}

$logDirectory = Join-Path $env:LOCALAPPDATA 'SurrealEngine\UT99-VR\Logs'
New-Item -ItemType Directory -Path $logDirectory -Force | Out-Null
$timestamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$logPath = Join-Path $logDirectory "ut99-vr-$timestamp.txt"

$launchArguments = @(
    '--autoplay'
    '--vr'
    '--vr-startmenu'
    "--logfile=$logPath"
)
if ($Map) {
    $launchArguments += "--url=$Map"
}
if ($LeftHanded) {
    $launchArguments += '--vr-lefthand'
}
if ($WeaponTuning) {
    $launchArguments += '--vrtune'
}

# The headset-validated release presentation is the world-fixed OpenXR quad.
# -QuadMenu remains accepted for compatibility with earlier test commands;
# -ProjectionMenu explicitly selects the head-locked stereo fallback.
if (-not $ProjectionMenu) {
    $launchArguments += '--vr-quadmenu'
}
if ($NoMenuLaser) {
    $launchArguments += '--vr-no-menu-laser'
}
if ($NoMenuControllers) {
    $launchArguments += '--vr-no-menu-controllers'
}
$launchArguments += $GamePath

Write-Host "This run's log:     $logPath"
& $engineExe @launchArguments
exit $LASTEXITCODE
