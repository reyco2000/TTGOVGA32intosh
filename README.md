# Cydintosh (VGA32 Edition)

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

Regardless of the build system you choose, start by performing the one-time project setup:

```bash
# 1. Navigate to the project directory
cd cydintosh

# 2. Initialize and download submodules (required for Musashi, umac, and softfloat)
git submodule update --init --recursive

# 3. Generate m68kops.c (Musashi core opcode tables)
# On Windows, you can run this in Git Bash or standard bash:
(cd external/umac && make prepare)

# 4. Create your local configuration file
cp include/user_config.h.tmpl include/user_config.h
```

Now, choose one of the three methods below to build and upload the firmware.

---

### Method A: Arduino CLI (Automated Build & Upload)

An automated Python script, [build_arduino.py](file:///c:/rey/cydintosh/build_arduino.py), is provided to handle all the necessary source-flattening, patching, and compilation. It automatically installs dependencies like **FabGL** and compiles cleanly using the custom configuration in `user_config.h`.

* **To compile only**:
  ```bash
  python build_arduino.py --compile
  ```
* **To compile and upload**:
  ```bash
  python build_arduino.py --compile --upload --port COM3
  ```
  *(Replace `COM3` with your board's serial port; e.g., `/dev/ttyUSB0` on Linux/macOS)*

---

### Method B: Arduino CLI / Arduino IDE (Manual)

Once the sketch has been prepared by the Python helper, it acts as a standard, fully self-contained Arduino sketch.

1. **Prepare the files**:
   ```bash
   python build_arduino.py --prepare
   ```
   *(This creates a self-contained sketch under `espvgatosh/`)*

2. **Compile using `arduino-cli` directly** (from inside the `espvgatosh/` directory):
   ```bash
   cd espvgatosh
   arduino-cli compile --fqbn esp32:esp32:esp32:PSRAM=enabled,PartitionScheme=huge_app .
   ```

3. **Upload using `arduino-cli` directly**:
   ```bash
   arduino-cli upload -p COM3 --fqbn esp32:esp32:esp32:PSRAM=enabled,PartitionScheme=huge_app .
   ```

> [!TIP]
> **Using Arduino IDE (GUI)**: You can open the generated sketch file [espvgatosh.ino](file:///c:/rey/cydintosh/espvgatosh/espvgatosh.ino) directly in the Arduino IDE (Version 2.x recommended). Select **ESP32 Dev Module** as your board, enable **PSRAM**, choose the **Huge APP** partition scheme, and use the standard verify/upload GUI buttons.

---

### Method C: PlatformIO (Command Line)

If you prefer using PlatformIO, you can compile and upload using the standard PlatformIO CLI:

```bash
# Build and upload firmware
pio run -e vga32 -t upload
```


## Mac Applications & ESP32-Mac IPC

The emulator supports dynamic IPC between the emulated Mac OS and the ESP32 via a shared memory-mapped interface at `0xF00000`. This enables homebrew Mac applications (such as Weather, WiFi Status, etc.) to query ESP32 host services:

| App | Description | Commands |
| --- | --- | --- |
| Weather | Displays weather info via MQTT | `GET_WEATHER_DATA` |
| WiFi | WiFi network scanning and status | `GET_WIFI_LIST`, `GET_WIFI_STATUS` |

### Weather App Integration

```mermaid
flowchart LR
    HA["Home Assistant<br/>(Automation)"] -->|"Publish<br>(1h)"| MB[("MQTT Broker")]
    MB -->|"Data"| ESP["ESP32"]
    ESP -->|"Data"| MAC["Weather App"]
    ESP -.->|"Subscribe"| MB
    MAC -.->|"Polling<br>(30s)"| ESP
    
    linkStyle 0,1,2 stroke:#4CAF50,color:#4CAF50
    linkStyle 3,4 stroke:#999
```

1. Home Assistant automation publishes weather data to MQTT every hour.
2. The ESP32 subscribes to MQTT and caches the data in memory.
3. The emulated Macintosh Weather app polls the ESP32 via IPC and renders the weather interface.

See `include/umac_ipc.h` for complete IPC protocol definitions.

### Updating the Disk Image Manually

To rebuild the homebrew Mac applications and pack them back into a disk image:

```bash
# Rebuild applications and update disk image
./tools/update-disk.sh path/to/disk.img
# Copy the updated disk.img back to your Micro SD card
```

## Development

```bash
# Format tracked C/C++ files
mise run format

# Check formatting without changes
mise run format:check

# Watch serial logs (PlatformIO)
pio device monitor

# Watch serial logs (Arduino CLI)
arduino-cli monitor -p COM3
```

## Acknowledgements

- [Musashi](https://github.com/kstenerud/Musashi) - m68k emulator
- [umac](https://github.com/evansm7/umac) - Mac Plus emulator core
- [pico-mac](https://github.com/evansm7/pico-mac) - Reference implementation for RP2040
- [FabGL](https://github.com/fdivitto/FabGL) - Graphics, VGA, and PS/2 input library for ESP32

## License

- **Software**: MIT
- **External libraries**: See respective licenses in `external/`
