# Toolchain Lock

Required Arduino CLI inputs:

- Arduino platform: `arduino:avr@1.8.4`
- FQBN: `arduino:avr:nano:cpu=atmega328`
- External libraries: none

Build command:

```powershell
.\tools\build-display-controller.ps1
```

Verification results:

- Local Arduino CLI build: OK, `5442 bytes (17%) of 30720 bytes`, RAM `400 bytes (19%) of 2048 bytes`
- Fresh clone Arduino CLI build from tag `firmware-v1.2.2-debug`: OK, `5442 bytes (17%) of 30720 bytes`, RAM `400 bytes (19%) of 2048 bytes`
