# TTGOVGA32intosh (VGA32 Edition)

A Macintosh Plus emulator port for ESP32 with VGA and PS/2 peripheral support.

- **Macintosh Plus Emulation**: Uses `umac` and the Musashi 68k emulator configured with **1MB of RAM**.
- **VGA Output**: Outputs a standard 640x480 VGA signal at 60Hz (displaying the emulated 512x342 Mac Plus screen centered with a clean black background).
- **PS/2 Keyboard & Mouse**: Connect standard PS/2 keyboard and mouse directly to the VGA32 board.
- **SD Card Support**: Loads the Macintosh system disk (`disk.img`) and ROM (`vMAC.ROM`) dynamically from a FAT16/FAT32 formatted Micro SD card.
- **Dynamic ROM Patching**: Patches the Macintosh Plus ROM in writable PSRAM at runtime, removing the need for pre-patching tools.

## Hardware BOM

| Component | Quantity | Notes |
| --- | :--- | :--- |
| TTGO VGA32 V1.4 Board | 1 | ESP32 board with built-in VGA port, PS/2 ports, and Micro SD slot |
| PS/2 Keyboard | 1 | Standard keyboard with PS/2 connector |
| PS/2 Mouse | 1 | Standard mouse with PS/2 connector |
| VGA Monitor | 1 | VGA display supporting 640x480 @ 60Hz resolution |
| Micro SD Card | 1 | Formatted as FAT16 or FAT32 |

## Prerequisites for Emulator

- **Mac Plus ROM v3** (4D1F8172, 128KB): Name it `vMAC.ROM` (original, unpatched) and place it on the root of your SD card.
- **System Disk Image**: A bootable System 6 HFS disk image named `disk.img` (e.g., a ~100MB HFS system disk) placed on the root of your SD card.

## Getting Started

1. **Hardware Setup**:
   - Plug the PS/2 Keyboard and PS/2 Mouse into the respective ports on the TTGO VGA32 board.
   - Connect the VGA Monitor to the VGA port.
   - Insert a Micro SD card containing your `disk.img` and `vMAC.ROM` at the root directory.

2. **Flash Firmware**:
   - Build and upload the firmware to the VGA32 board (see [Building](#building)).

## Building

This is a regular Arduino sketch: `TTGOVGA32intosh.ino` at the repository root, with all code under `src/`. The umac and Musashi emulator sources (including the generated `m68kops.c`) are bundled in `src/emu/`, so there are no submodules or code-generation steps.

### Requirements

- [arduino-cli](https://arduino.github.io/arduino-cli/) (or Arduino IDE 2.x)
- ESP32 Arduino core **2.0.x** (tested with 2.0.17; FabGL does not support core 3.x)
- FabGL library (tested with 1.0.9)

```bash
arduino-cli core install esp32:esp32@2.0.17
arduino-cli lib install FabGL
```

The repository folder must be named `TTGOVGA32intosh` (Arduino requires the sketch folder to match the `.ino` name).

### Arduino CLI

Board options (ESP32 Dev Module, PSRAM enabled, Huge APP partition scheme, debug log level) and the upload port are set in [sketch.yaml](sketch.yaml), so from the repository root:

```bash
# Compile
arduino-cli compile

# Compile and upload
arduino-cli compile --upload

# Watch serial logs
arduino-cli monitor -c baudrate=115200
```

If your board shows up on a different port, edit `default_port` in `sketch.yaml` or pass `-p /dev/ttyUSB0` (e.g. `COM3` on Windows).

### Arduino IDE

Open `TTGOVGA32intosh.ino`, select **ESP32 Dev Module**, set **PSRAM: Enabled** and **Partition Scheme: Huge APP (3MB No OTA/1MB SPIFFS)**, then use Verify/Upload.

### Configuration

Emulator settings (`UMAC_MEMSIZE`, `DISP_WIDTH`, `DISP_HEIGHT`, `ENABLE_DASM`) are in [src/user_config.h](src/user_config.h).

## Development

```bash
# Format the project's own C/C++ sources (src/emu/ is vendored and excluded)
clang-format -i src/*.c src/*.cpp src/*.h
```

## Acknowledgements

- [Musashi](https://github.com/kstenerud/Musashi) - m68k emulator
- [umac](https://github.com/evansm7/umac) - Mac Plus emulator core (via the [likeablob/umac](https://github.com/likeablob/umac) ESP32 fork)
- [pico-mac](https://github.com/evansm7/pico-mac) - Reference implementation for RP2040
- [FabGL](https://github.com/fdivitto/FabGL) - Graphics, VGA, and PS/2 input library for ESP32
- [Reinaldo Torres](https://github.com/reyco2000) - CoCoByte Club, port to TTGOVGA memory handling improvements

## License

- **Software**: MIT
- **Bundled emulator core** (`src/emu/`): umac and Musashi (MIT-style), Basilisk II-derived `disc.c` (GPLv2), SoftFloat 2b — see [src/emu/README.md](src/emu/README.md)
