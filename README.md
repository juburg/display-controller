# DisplayController

Arduino firmware repository for the 7-segment display controller.

Production source:

- `DisplayControllerV1.2.2/DisplayController_V122_debug.ino`
- Firmware version from source header: `V1.2.2 DEBUG`
- Board from compiler log: `Arduino Nano`
- Arduino platform: `arduino:avr@1.8.4`

Build:

```powershell
.\tools\build-display-controller.ps1
```

Pinned FQBN:

```text
arduino:avr:nano:cpu=atmega328
```

The build script stages the sketch into a temporary folder named `DisplayController_V122_debug`, because Arduino requires the sketch directory name to match the main `.ino` file.
