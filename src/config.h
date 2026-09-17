#ifndef TTGOVGA32INTOSH_CONFIG_H
#define TTGOVGA32INTOSH_CONFIG_H

// Build target selection.
//
//   BUILD_TARGET_STANDALONE  Normal build. Flashed over USB with esptool /
//                            `arduino-cli compile --upload`; the sketch owns
//                            the whole device and boots directly.
//
//   BUILD_TARGET_BOOTLOADER  Built to be launched by ESP32_Bootloader
//                            (https://github.com/ESP-WORKS/ESP32_Bootloader).
//                            The bootloader itself lives in the `factory`
//                            partition and flashes this app into `ota_0` at
//                            0x130000 from the SD card. The app erases the
//                            `otadata` partition on startup so the next
//                            power-up returns to the bootloader menu instead
//                            of booting straight back into the emulator.
//
// Either edit the default below, or override it at compile time without
// touching this file:
//
//   arduino-cli compile --build-property \
//       compiler.cpp.extra_flags=-DBUILD_TARGET=1
//
// `tools/package-bootloader.sh` does exactly that and packages the result for
// the SD card.
#define BUILD_TARGET_STANDALONE 0
#define BUILD_TARGET_BOOTLOADER 1

#ifndef BUILD_TARGET
#define BUILD_TARGET BUILD_TARGET_STANDALONE
#endif

// Size of the `ota_0` partition in the ESP32_Bootloader partition table
// (2816 KB). An app image larger than this will not fit and the bootloader
// cannot flash it.
#define BOOTLOADER_OTA0_MAX_BYTES 0x2C0000

#endif
