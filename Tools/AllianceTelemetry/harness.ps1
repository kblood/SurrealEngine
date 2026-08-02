# AllianceTelemetry A/B runner. Usage:
#   .\harness.ps1 -Build
#   .\harness.ps1 -Run retail -Tag myrun
#   .\harness.ps1 -Run both -Tag myrun -Ini @{ AutoQuitDelay = 120 } -TimeoutSec 260
param(
  [switch]$Build,
  [ValidateSet('retail','se','both')][string]$Run,
  [string]$Tag = 'run',
  [hashtable]$Ini = @{},
  [int]$TimeoutSec = 120,
  [string]$OutDir  = "$PSScriptRoot\captures",
  [string]$GameDir = 'C:\DXGame',
  [string]$Engine  = "$PSScriptRoot\..\..\build\Release\SurrealEngine.exe",
  [string]$Map     = '01_NYC_UNATCOIsland'
)

$ErrorActionPreference = 'Stop'
$SRC = "$PSScriptRoot\Classes"
$DST = "$GameDir\AllianceTelemetry\Classes"
$SYS = "$GameDir\System"
$LOG = "$GameDir\Logs\alliancetelemetry.tmp"

function Kill-Engines {
  Get-Process DeusEx,SurrealEngine -ErrorAction SilentlyContinue | ForEach-Object {
    $_.Kill(); $_.WaitForExit(5000) | Out-Null
  }
}

if ($Build) {
  Kill-Engines
  # ucc compiles from DST, a real directory -- not a junction to the repo.
  Copy-Item "$SRC\*.uc" "$DST\" -Force
  Remove-Item "$SYS\AllianceTelemetry.u" -Force -ErrorAction SilentlyContinue
  Push-Location $SYS
  $out = & "$SYS\ucc.exe" make 2>&1 | Out-String
  Pop-Location
  if ($out -match 'Success') { "BUILD OK" }
  else { $out -split "`n" | Select-String -Pattern 'Error|Warning|Fail' | Select-Object -First 20; throw "BUILD FAILED" }
}

# AllianceTelemetry.ini persists between runs, so a key left out of -Ini keeps
# whatever the previous run set it to. Restate every key that matters.
if ($Ini.Count) {
  $p = "$SYS\AllianceTelemetry.ini"
  $t = Get-Content $p -Raw
  foreach ($k in $Ini.Keys) {
    if ($t -match "(?m)^$k=") { $t = $t -replace "(?m)^$k=.*", "$k=$($Ini[$k])" }
    else { $t = $t.TrimEnd() + "`r`n$k=$($Ini[$k])`r`n" }
  }
  Set-Content $p $t -NoNewline
}

function Invoke-Engine([string]$which) {
  Kill-Engines
  Remove-Item $LOG -Force -ErrorAction SilentlyContinue
  # Retail shows a modal "Recovery Mode" window if this marker survives a killed run.
  Remove-Item "$SYS\Running.ini" -Force -ErrorAction SilentlyContinue
  $url = "$Map`?game=AllianceTelemetry.AllianceTelemetryGameInfo"
  if ($which -eq 'retail') {
    $p = Start-Process -FilePath "$SYS\DeusEx.exe" -ArgumentList $url,'-nosound','-windowed' -WorkingDirectory $SYS -PassThru
  } else {
    $p = Start-Process -FilePath $Engine -ArgumentList '--autoplay',"--url=$url","`"$GameDir`"" -WorkingDirectory $SYS -PassThru
  }
  $p.WaitForExit($TimeoutSec * 1000) | Out-Null
  $status = if (-not $p.HasExited) {
    $p.Refresh(); $title = $p.MainWindowTitle; $p.Kill(); $p.WaitForExit(10000) | Out-Null; "TIMEOUT (window='$title')"
  } else { "exit $($p.ExitCode)" }
  if (-not (Test-Path $OutDir)) { New-Item -ItemType Directory $OutDir | Out-Null }
  $dest = "$OutDir\$Tag-$which.raw"
  if (Test-Path $LOG) {
    # A killed engine can still hold the log handle for a moment after exit.
    foreach ($try in 1..10) {
      try { Copy-Item $LOG $dest -Force; break } catch { Start-Sleep -Milliseconds 500 }
    }
    "$which : $status, $((Get-Item $dest).Length) bytes -> $Tag-$which.raw"
  }
  else { "$which : $status, NO LOG" }
}

if ($Run -eq 'both') { Invoke-Engine retail; Invoke-Engine se }
elseif ($Run) { Invoke-Engine $Run }
