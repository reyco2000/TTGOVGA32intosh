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
   - Flash a prebuilt release (see [Flashing a Prebuilt Release](#flashing-a-prebuilt-release)), or build and upload it yourself (see [Building](#building)).

## Flashing a Prebuilt Release

Ready-to-flash firmware is published on the [Releases page](https://github.com/reyco2000/TTGOVGA32intosh/releases). Each release contains:

| File | Description |
| --- | --- |
| `TTGOVGA32intosh-vX.Y.Z-merged.bin` | Complete image (bootloader + partitions + app), flashed at offset `0x0`. **Recommended.** |
| `TTGOVGA32intosh-vX.Y.Z-app.bin` | Application only (offset `0x10000`) |
| `TTGOVGA32intosh-vX.Y.Z-bootloader.bin` | Bootloader (offset `0x1000`) |
| `TTGOVGA32intosh-vX.Y.Z-partitions.bin` | Partition table (offset `0x8000`) |
| `boot_app0.bin` | OTA data (offset `0xe000`) |
| `SHA256SUMS.txt` | Checksums for all files |

The examples below use `v0.1.0`; replace it with the release you downloaded.

### 1. Download

Download `TTGOVGA32intosh-v0.1.0-merged.bin` and `SHA256SUMS.txt` from the release page, or with the GitHub CLI:

```bash
gh release download v0.1.0 -R reyco2000/TTGOVGA32intosh
```

Optionally verify the download (Linux/macOS):

```bash
sha256sum -c --ignore-missing SHA256SUMS.txt
```

### 2. Install esptool

```bash
pip install esptool
```

(If you have the Arduino ESP32 core installed, esptool is already bundled with it.)

### 3. Connect the board

Connect the TTGO VGA32 with a USB cable and find its serial port:

- **Linux**: `/dev/ttyACM0` or `/dev/ttyUSB0` (your user must be in the `dialout` group)
- **macOS**: `/dev/cu.usbserial-*` or `/dev/cu.wchusbserial*`
- **Windows**: `COM3`, `COM4`, ... (see Device Manager → Ports)

### 4. Flash

**Option A — merged image (recommended):**

```bash
esptool.py --chip esp32 --port /dev/ttyACM0 --baud 921600 write_flash 0x0 TTGOVGA32intosh-v0.1.0-merged.bin
```

**Option B — separate images:**

```bash
esptool.py --chip esp32 --port /dev/ttyACM0 --baud 921600 write_flash -z \
  --flash_mode dio --flash_freq 80m --flash_size 4MB \
  0x1000  TTGOVGA32intosh-v0.1.0-bootloader.bin \
  0x8000  TTGOVGA32intosh-v0.1.0-partitions.bin \
  0xe000  boot_app0.bin \
  0x10000 TTGOVGA32intosh-v0.1.0-app.bin
```

On Windows, use your COM port (e.g. `--port COM3`) and write Option B on one line. With newer esptool versions the command is `esptool` instead of `esptool.py`.

**Option C — web browser (no install):** open the [Espressif ESP Web Flasher](https://espressif.github.io/esptool-js/) in Chrome or Edge, click **Connect**, select the board's port, add `TTGOVGA32intosh-v0.1.0-merged.bin` at address `0x0`, and click **Program**.

If flashing fails to connect, retry with a lower speed (`--baud 115200`).

### 5. Verify

Press the reset button (or unplug/replug USB) and watch the serial log at 115200 baud, e.g. `arduino-cli monitor -p /dev/ttyACM0 -c baudrate=115200` or any serial terminal. A healthy boot ends with:

```
>>> ROM patched successfully!
>>> Initializing UMAC Core...
>>> Starting emulator loop!
```

If you see `Failed to load ROM from SD card`, check that `vMAC.ROM` and `disk.img` are in the root of the SD card.

> [!NOTE]
> If `disk.img` is missing, the firmware creates an **empty** `disk.img` on the card (the log shows `Size: 0 bytes`). Replace it with a real system disk image.

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

- [likeablob/cydintosh](https://github.com/likeablob/cydintosh) - Original Cydintosh project (Macintosh Plus emulator for the ESP32 Cheap Yellow Display) this port is based on
- [Musashi](https://github.com/kstenerud/Musashi) - m68k emulator
- [umac](https://github.com/evansm7/umac) - Mac Plus emulator core (via the [likeablob/umac](https://github.com/likeablob/umac) ESP32 fork)
- [pico-mac](https://github.com/evansm7/pico-mac) - Reference implementation for RP2040
- [FabGL](https://github.com/fdivitto/FabGL) - Graphics, VGA, and PS/2 input library for ESP32
- [Reinaldo Torres](https://github.com/reyco2000) - CoCoByte Club, port to TTGOVGA memory handling improvements

## License

- **Software**: MIT
- **Bundled emulator core** (`src/emu/`): umac and Musashi (MIT-style), Basilisk II-derived `disc.c` (GPLv2), SoftFloat 2b — see [src/emu/README.md](src/emu/README.md)
