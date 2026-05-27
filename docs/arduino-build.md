# Arduino Build Reference

Source log:

- `docs/arduino-ide-1.8.19-build.log`

Original IDE build:

- Arduino IDE: `1.8.19`
- Board: `arduino:avr:nano:cpu=atmega328`
- Platform: `arduino:avr@1.8.4`
- Core: `arduino`
- Sketch path in log: `DisplayController_V121_debug.ino`
- Repository sketch: `DisplayController_V122_debug.ino`
- Program size: `5442 bytes (17%) of 30720 bytes`
- Dynamic memory: `400 bytes (19%) of 2048 bytes`

External libraries:

- none

## External Flash Package

Create the beginner package with:

```powershell
.\tools\package-display-controller-flasher.ps1
```

The generated package flashes via the Arduino Nano bootloader:

```text
avrdude.exe -C avrdude.conf -v -patmega328p -carduino -P<COM> -b115200 -D -Uflash:w:DisplayController_V122_debug.ino.hex:i
```

The documented target is `arduino:avr:nano:cpu=atmega328`, which uses the Nano
ATmega328P bootloader at `115200` baud. For old Nano bootloaders the generated
batch also accepts `57600` as second argument.
