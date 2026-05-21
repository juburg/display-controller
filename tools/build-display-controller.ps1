param(
  [string]$ArduinoCli = $env:ARDUINO_CLI,
  [switch]$SkipCoreInstall
)

$ErrorActionPreference = 'Stop'

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot '..')
$fqbn = 'arduino:avr:nano:cpu=atmega328'
$core = 'arduino:avr@1.8.4'

if (-not $ArduinoCli) {
  $candidates = @(
    'C:\Program Files\Arduino CLI\arduino-cli.exe',
    'C:\Users\juerg\AppData\Local\Programs\Arduino CLI\arduino-cli.exe',
    'arduino-cli'
  )

  foreach ($candidate in $candidates) {
    $cmd = Get-Command $candidate -ErrorAction SilentlyContinue
    if ($cmd) {
      $ArduinoCli = $cmd.Source
      break
    }
  }
}

if (-not $ArduinoCli) {
  throw 'arduino-cli not found. Set ARDUINO_CLI or pass -ArduinoCli.'
}

$cliState = Join-Path $repoRoot '.arduino-cli-display-controller'
$buildRoot = Join-Path $repoRoot 'build'
$stagedSketch = Join-Path $buildRoot 'DisplayController_V122_debug'
$sourceSketch = Join-Path $repoRoot 'DisplayControllerV1.2.2'
$buildPath = Join-Path $buildRoot 'firmware-DisplayController_V122_debug'

New-Item -ItemType Directory -Force -Path $cliState, $buildRoot, $stagedSketch, $buildPath | Out-Null

$configFile = Join-Path $cliState 'arduino-cli.yaml'
if (-not (Test-Path $configFile)) {
  & $ArduinoCli config init --dest-dir $cliState | Out-Host
}

if (-not $SkipCoreInstall) {
  & $ArduinoCli --config-file $configFile core update-index | Out-Host
  & $ArduinoCli --config-file $configFile core install $core | Out-Host
}

Get-ChildItem -Path $stagedSketch -Force | Remove-Item -Recurse -Force
Copy-Item -Path (Join-Path $sourceSketch '*') -Destination $stagedSketch -Recurse -Force

$sourceIno = Join-Path $stagedSketch 'DisplayController_V122_debug.ino'
if (-not (Test-Path $sourceIno)) {
  throw "Expected sketch not found: $sourceIno"
}

& $ArduinoCli `
  --config-file $configFile `
  compile `
  --fqbn $fqbn `
  --build-path $buildPath `
  $stagedSketch
