param(
  [switch]$SkipBuild
)

$ErrorActionPreference = 'Stop'

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot = Split-Path -Parent $scriptDir
$buildScript = Join-Path $scriptDir 'build-display-controller.ps1'
$buildPath = Join-Path $repoRoot 'build\firmware-DisplayController_V122_debug'
$packageDir = Join-Path $repoRoot 'dist\DisplayController_V122_Flashpaket'
$zipFile = Join-Path $repoRoot 'dist\DisplayController_V122_Flashpaket.zip'
$firmwareHex = Join-Path $buildPath 'DisplayController_V122_debug.ino.hex'

function Find-FirstExistingFile {
  param(
    [string[]]$Candidates,
    [string]$Description
  )

  foreach ($candidate in $Candidates) {
    if (Test-Path -LiteralPath $candidate) {
      return $candidate
    }
  }

  throw "$Description not found. Run tools\build-display-controller.ps1 first."
}

if (-not $SkipBuild) {
  & $buildScript -SkipCoreInstall
  if ($LASTEXITCODE -ne 0) {
    throw "DisplayController build failed with exit code ${LASTEXITCODE}"
  }
}

$arduinoDataRoots = @(
  (Join-Path $env:LOCALAPPDATA 'Arduino15'),
  (Join-Path $env:USERPROFILE 'Documents\ArduinoData')
)

$avrdudeCandidates = foreach ($root in $arduinoDataRoots) {
  Join-Path $root 'packages\arduino\tools\avrdude\6.3.0-arduino17\bin\avrdude.exe'
}

$avrdudeConfCandidates = foreach ($root in $arduinoDataRoots) {
  Join-Path $root 'packages\arduino\tools\avrdude\6.3.0-arduino17\etc\avrdude.conf'
}

$libusbCandidates = foreach ($root in $arduinoDataRoots) {
  Join-Path $root 'packages\arduino\tools\avrdude\6.3.0-arduino17\bin\libusb0.dll'
}

$avrdude = Find-FirstExistingFile -Candidates $avrdudeCandidates -Description 'avrdude.exe'
$avrdudeConf = Find-FirstExistingFile -Candidates $avrdudeConfCandidates -Description 'avrdude.conf'
$libusb = Find-FirstExistingFile -Candidates $libusbCandidates -Description 'libusb0.dll'

if (-not (Test-Path -LiteralPath $firmwareHex)) {
  throw "Required firmware file missing: $firmwareHex"
}

if (Test-Path -LiteralPath $packageDir) {
  Remove-Item -LiteralPath $packageDir -Recurse -Force
}

New-Item -ItemType Directory -Path $packageDir -Force | Out-Null

Copy-Item -LiteralPath $firmwareHex -Destination $packageDir -Force
Copy-Item -LiteralPath $avrdude -Destination (Join-Path $packageDir 'avrdude.exe') -Force
Copy-Item -LiteralPath $avrdudeConf -Destination (Join-Path $packageDir 'avrdude.conf') -Force
Copy-Item -LiteralPath $libusb -Destination (Join-Path $packageDir 'libusb0.dll') -Force

$flashBat = @'
@echo off
setlocal
cd /d "%~dp0"

echo DisplayController Firmware Flash fuer Arduino Nano
echo.
echo Schritt 1: DisplayController per USB anschliessen.
echo Schritt 2: COM-Port im Windows-Geraetemanager pruefen.
echo.

if "%~1"=="" (
  echo Gefundene COM-Ports:
  powershell -NoProfile -ExecutionPolicy Bypass -Command "Get-CimInstance Win32_SerialPort | Select-Object DeviceID,Description | Format-Table -AutoSize"
  echo.
  set /p PORT=COM-Port eingeben, z.B. COM15: 
) else (
  set PORT=%~1
)

if "%PORT%"=="" (
  echo Kein COM-Port angegeben.
  pause
  exit /b 1
)

if "%~2"=="" (
  set BAUD=57600
) else (
  set BAUD=%~2
)

echo.
echo Flashe DisplayController_V122 auf %PORT% mit %BAUD% Baud ...
echo.
avrdude.exe -C avrdude.conf -v -patmega328p -carduino -P%PORT% -b%BAUD% -D -Uflash:w:DisplayController_V122_debug.ino.hex:i

if errorlevel 1 (
  echo.
  echo FEHLER: Flashen fehlgeschlagen.
  echo USB-Kabel, COM-Port und Nano-Bootloader pruefen.
  echo Optional neuer Nano-Bootloader: flash-DisplayController.bat %PORT% 115200
  pause
  exit /b 1
)

echo.
echo Fertig. Der DisplayController wurde neu gestartet.
pause
'@

Set-Content -LiteralPath (Join-Path $packageDir 'flash-DisplayController.bat') -Value $flashBat -Encoding ascii

$readme = @'
DisplayController Firmware Flashpaket fuer Arduino Nano
=======================================================

Dieses Paket benoetigt keine Arduino IDE.

Inhalt
------
- DisplayController_V122_debug.ino.hex   Firmware
- avrdude.exe                            Flashprogramm
- avrdude.conf                           avrdude-Konfiguration
- libusb0.dll                            Laufzeitdatei fuer avrdude
- flash-DisplayController.bat            Einfacher Windows-Flasher

Normaler Weg
------------
1. DisplayController per USB anschliessen.
2. Im Windows-Geraetemanager den COM-Port merken, z.B. COM15.
3. flash-DisplayController.bat doppelklicken.
4. COM-Port eingeben.
5. Warten bis "Fertig" erscheint.

Direkt per Kommandozeile
------------------------
flash-DisplayController.bat COM15

Neuer Nano-Bootloader
---------------------
Falls der normale Weg fehlschlaegt:

flash-DisplayController.bat COM15 115200

Fehlerhilfe
-----------
- USB-Kabel pruefen. Manche Kabel sind nur Ladekabel.
- COM-Port im Geraetemanager erneut pruefen.
- Default ist Arduino Nano ATmega328P mit altem Bootloader bei 57600 Baud.
- Bei Nano mit neuem Bootloader 115200 Baud versuchen.
'@

Set-Content -LiteralPath (Join-Path $packageDir 'README_KURZ.txt') -Value $readme -Encoding ascii

if (Test-Path -LiteralPath $zipFile) {
  Remove-Item -LiteralPath $zipFile -Force
}

$packageFiles = Get-ChildItem -LiteralPath $packageDir -File
Compress-Archive -LiteralPath $packageFiles.FullName -DestinationPath $zipFile -Force

Get-Item -LiteralPath $zipFile, $packageDir |
  Select-Object FullName, Length, LastWriteTime
