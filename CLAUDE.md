# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

Macintosh Plus emulator (umac + Musashi 68k core) for the **TTGO VGA32 V1.4** (ESP32 + PSRAM). Output is 640x480 VGA via FabGL with the 512x342 Mac screen centered; input is PS/2 keyboard/mouse; ROM (`vMAC.ROM`, unpatched Mac Plus v3, 4D1F8172) and `disk.img` are read from the SD card root at runtime.

It is a plain Arduino sketch: `TTGOVGA32intosh.ino` (empty entry point) at the repo root, code under `src/`. The repo folder must be named `TTGOVGA32intosh`.

The project is a fork of **Cydintosh** (ESP32 "Cheap Yellow Display" 240x320 port). Remaining leftovers **not wired into the firmware**: WiFi/MQTT/Home Assistant/weather settings in `src/user_config.h`; `homeassistant/` automations exist only locally (gitignored).

## Build / flash / monitor

Requires ESP32 Arduino core **2.0.x** (2.0.17 installed; FabGL doesn't support 3.x) and the FabGL library (1.0.9). No tests; clang-format only.

```bash
arduino-cli compile                 # FQBN/port come from sketch.yaml
arduino-cli compile --upload        # port default /dev/ttyACM0 (CH9102 USB-serial)
arduino-cli monitor -c baudrate=115200
```

A full compile takes ~3 min on the Pi. `sketch.yaml` sets `esp32:esp32:esp32:PSRAM=enabled,PartitionScheme=huge_app,DebugLevel=debug`. Don't add a `partitions.csv` at the root — arduino-esp32 would use it instead of `huge_app`.

arduino-cli walks the **entire sketch folder** when copying sources (it failed on a dangling symlink once), and compiles every `.c`/`.cpp` under `src/` recursively. So: no stray/generator `.c` files under `src/`, no broken symlinks anywhere in the repo, and files meant to be `#include`d rather than compiled must not end in `.c` (hence `m68kfpu.c.h`). Arduino adds no include paths for `src/` subfolders — use relative includes (`"emu/umac.h"`, `"../user_config.h"`).

The `.gitignore` is an allowlist (`/*` then `!` entries): new top-level files/dirs must be allowlisted to be tracked. `data/` and `vMac.ROM` (local SD card images) are intentionally untracked.

## Architecture

- `src/main.cpp` — Arduino `setup()` starts PS/2 (`KeyboardPort0_MousePort1`), launches the display task on core 0 and `umac_task` on core 1. `umac_task` allocates Mac RAM in PSRAM (halving from `UMAC_MEMSIZE` down to 128KB on failure) and the 1bpp framebuffer, mounts SD/opens `disk.img` (`disc_setup`), loads `/sdcard/vMAC.ROM` into PSRAM, `rom_patch()`es it, then `umac_init()`. The loop runs `umac_loop()` (40000 cycles), fires `umac_vsync_event()` + `video_update()` every ~133000 cycles (1Hz event every 60 frames), and polls FabGL for mouse deltas (Y inverted) and keys. Keys map ASCII→Mac keycode via `map_ascii_to_mac()` plus explicit shift/option(Alt)/command(Ctrl); sent as `(code << 1) | 1`.
- `src/display.cpp` — FreeRTOS task owning `fabgl::VGA2Controller`. Mac 1bpp (MSB first, 1=black) matches VGA2 scanlines with palette 0=white/1=black, so rows are `memcpy`'d into `getScanline()` at a centering offset. Woken by task notification from `video_update()` (`src/video.c` is the glue).
- `src/disc_sd.c` — SDSPI mount at `/sdcard` (MOSI 12, MISO 2, CLK 14, CS 13) and umac disc read/write callbacks on the image file. If `disk.img` is missing it **creates an empty file** on the card.
- `src/user_config.h` — emulator settings (`UMAC_MEMSIZE`, `DISP_WIDTH/HEIGHT`, `ENABLE_DASM`) plus unused CYD network placeholders. Tracked; don't put real credentials in it.
- `src/emu/` — vendored umac (likeablob ESP fork) + Musashi + SoftFloat with local modifications; origin commits and change list in `src/emu/README.md`. `machw.h` maps the Mac memory map and redirects framebuffer addresses to a separate internal-RAM buffer; `m68kconf.h` configures Musashi (IRAM fast functions). `m68kops.c` is generated — regenerate with umac's `make prepare` only if changing Musashi.

Emulator-core gotchas:
- `m68kconf.h` uses `#ifdef ENABLE_DASM`; wherever `user_config.h` was included first (it defines it as 0) the per-instruction `cpu_instr_callback()` hook is compiled in (it returns early).
- umac's keyboard holds a single pending event (`kbd_pending_evt`, no queue); fast key events overwrite each other.
- Unmapped I/O (including the old Cydintosh IPC window at `0xF00000`) reads 0 / logs "Ignoring write" rather than crashing. Boot logs `Unknown SonyControl(20/21/22)` and `accRun: Not supported!` normally.

## Formatting

`.clang-format` is provided; `src/emu/` is excluded via `.clang-format-ignore`. Run `clang-format -i` on changed project files only.
