extern "C" {
#include "disc_sd.h"
}

#include "esp_heap_caps.h"
#include "esp_log.h"

#include "esp_system.h"
#include "esp_timer.h"

extern "C" {
#include "emu/rom.h"
#include "emu/umac.h"
#include "video.h"
unsigned int m68k_get_reg(void* context, int regnum);
}
#include "display.h"

#include "config.h"
#include "user_config.h"

#if BUILD_TARGET == BUILD_TARGET_BOOTLOADER
#include "esp_ota_ops.h"
#include "esp_partition.h"
#endif

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <Arduino.h>
#include "fabgl.h"

static const char *TAG = "ttgovga32intosh";

static const uint8_t *rom_mmap = NULL;

#define UMAC_ROM_SIZE 0x20000
#define FB_SIZE       (DISP_WIDTH * DISP_HEIGHT / 8)

static const uint8_t umac_disc_fallback[] = {0x4c, 0x4b};
static disc_descr_t discs[DISC_NUM_DRIVES] = {0};

fabgl::VGA2Controller DisplayController;
fabgl::PS2Controller PS2Controller;

static void disc_setup(void) {
    if (disc_sd_open(&discs[0], "disk.img", 0) == 0) {
        ESP_LOGI(TAG, "Using SD card disk image");
        return;
    }

    ESP_LOGW(TAG, "No disk image found, using fallback");
    discs[0].base      = (uint8_t *)umac_disc_fallback;
    discs[0].read_only = 1;
    discs[0].size      = sizeof(umac_disc_fallback);
}

static int rom_load_from_sd(const uint8_t **rom_ptr, size_t *rom_size) {
    uint8_t *rom_buf = (uint8_t *)heap_caps_malloc(UMAC_ROM_SIZE, MALLOC_CAP_SPIRAM);
    if (!rom_buf) {
        ESP_LOGE(TAG, "Failed to allocate ROM buffer in PSRAM");
        return -1;
    }
    
    FILE *f = fopen("/sdcard/vMAC.ROM", "rb");
    if (!f) {
        ESP_LOGE(TAG, "Failed to open /sdcard/vMAC.ROM. Is it on the SD card?");
        free(rom_buf);
        return -1;
    }
    
    size_t bytes_read = fread(rom_buf, 1, UMAC_ROM_SIZE, f);
    fclose(f);
    
    if (bytes_read == 0) {
        ESP_LOGE(TAG, "Failed to read ROM");
        free(rom_buf);
        return -1;
    }
    
    *rom_ptr = rom_buf;
    *rom_size = UMAC_ROM_SIZE;
    printf(">>> ROM loaded from /sdcard/vMAC.ROM to PSRAM\n");
    return 0;
}

// Basic map from ASCII to Mac Plus keycodes
static uint8_t map_ascii_to_mac(uint8_t ascii) {
    if (ascii >= 'a' && ascii <= 'z') ascii -= 32;
    switch(ascii) {
        case 'A': return 0x00; case 'S': return 0x01; case 'D': return 0x02; case 'F': return 0x03;
        case 'H': return 0x04; case 'G': return 0x05; case 'Z': return 0x06; case 'X': return 0x07;
        case 'C': return 0x08; case 'V': return 0x09; case 'B': return 0x0B; case 'Q': return 0x0C;
        case 'W': return 0x0D; case 'E': return 0x0E; case 'R': return 0x0F; case 'Y': return 0x10;
        case 'T': return 0x11; case '1': return 0x12; case '2': return 0x13; case '3': return 0x14;
        case '4': return 0x15; case '6': return 0x16; case '5': return 0x17; case '=': return 0x18;
        case '9': return 0x19; case '7': return 0x1A; case '-': return 0x1B; case '8': return 0x1C;
        case '0': return 0x1D; case ']': return 0x1E; case 'O': return 0x1F; case 'U': return 0x20;
        case '[': return 0x21; case 'I': return 0x22; case 'P': return 0x23; case 'L': return 0x25;
        case 'J': return 0x26; case '\'': return 0x27; case 'K': return 0x28; case ';': return 0x29;
        case '\\': return 0x2A; case ',': return 0x2B; case '/': return 0x2C; case 'N': return 0x2D;
        case 'M': return 0x2E; case '.': return 0x2F; case ' ': return 0x31; case '\r': return 0x24;
        case '\n': return 0x24; case '\b': return 0x33; case 27: return 0x35;
        default: return 0xFF;
    }
}

static void umac_task(void *arg) {
    Serial.println(">>> UMAC Task Started! Allocating Memory...");
    size_t psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    Serial.printf(">>> PSRAM free: %d bytes (%dKB)\n", psram_free, psram_free / 1024);
    Serial.printf(">>> Requested RAM_SIZE = %d bytes (%dKB)\n", (int)(1024 * UMAC_MEMSIZE), (int)UMAC_MEMSIZE);
    
    // Try to allocate the requested size, fall back to smaller sizes
    int ram_kb = UMAC_MEMSIZE;  // in KB
    uint8_t *umac_ram = NULL;
    while (ram_kb >= 128 && !umac_ram) {
        umac_ram = (uint8_t *)heap_caps_calloc(1, ram_kb * 1024, MALLOC_CAP_SPIRAM);
        if (!umac_ram) {
            Serial.printf(">>> Failed to allocate %dKB, trying %dKB...\n", ram_kb, ram_kb / 2);
            ram_kb /= 2;
        }
    }
    if (!umac_ram) {
        Serial.println(">>> ERROR: Failed to allocate any PSRAM!");
        ESP_LOGE(TAG, "Failed to allocate PSRAM!");
        vTaskDelete(NULL);
    }
    Serial.printf(">>> UMAC RAM Allocated OK: %dKB\n", ram_kb);

    uint8_t *umac_fb = (uint8_t *)heap_caps_calloc(1, FB_SIZE, MALLOC_CAP_8BIT);
    if (!umac_fb) {
        Serial.println(">>> ERROR: Failed to allocate framebuffer!");
        ESP_LOGE(TAG, "Failed to allocate framebuffer!");
        vTaskDelete(NULL);
    }
    Serial.println(">>> UMAC Framebuffer Allocated OK.");

    Serial.println(">>> Setting up Disk Images & SD Card...");
    disc_setup();

    size_t rom_mapped_size;
    Serial.println(">>> Loading ROM from SD Card (/sdcard/vMAC.ROM)...");
    if (rom_load_from_sd(&rom_mmap, &rom_mapped_size) != 0) {
        Serial.println(">>> ERROR: Failed to load ROM from SD card!");
        ESP_LOGE(TAG, "Failed to load ROM");
        vTaskDelete(NULL);
    }
    Serial.printf(">>> ROM Loaded OK. Size: %d bytes.\n", rom_mapped_size);

    // Patch ROM in place (now that it's in writable PSRAM)
    Serial.println(">>> Patching ROM...");
    int patch_result = rom_patch((uint8_t *)rom_mmap);
    if (patch_result != 0) {
        Serial.println(">>> WARNING: ROM patching failed! Unknown ROM version?");
    } else {
        Serial.println(">>> ROM patched successfully!");
    }

    Serial.println(">>> Initializing UMAC Core...");
    umac_init(umac_ram, (void *)rom_mmap, umac_fb, discs);

    Serial.println(">>> Starting emulator loop!");
    video_init((uint32_t *)umac_fb);

    int emulated_cycles = 0;
    int frame_count = 0;
    const int cycles_per_frame = 133000;

    while (1) {
        umac_loop();
        emulated_cycles += 40000;

        if (emulated_cycles >= cycles_per_frame) {
            umac_vsync_event();
            video_update();
            emulated_cycles -= cycles_per_frame;

            frame_count++;

            if (frame_count >= 60) {
                umac_1hz_event();
                unsigned int pc = m68k_get_reg(NULL, 16);
                // Serial.printf(">>> [EMU] 1s PC=%08x\n", pc);
                frame_count = 0;
            }

            // Read Mouse - use delta events for proper relative movement
            if (PS2Controller.mouse()) {
                fabgl::MouseDelta delta;
                while (PS2Controller.mouse()->getNextDelta(&delta, 0)) {
                    int buttons = delta.buttons.left ? 1 : 0;
                    // Mac Y axis is inverted relative to PS/2
                    umac_mouse(delta.deltaX, -delta.deltaY, buttons);
                }
            }

            // Read Keyboard
            if (PS2Controller.keyboard()) {
                fabgl::VirtualKeyItem vki;
                while (PS2Controller.keyboard()->getNextVirtualKey(&vki, 0)) {
                    uint8_t mac_code = map_ascii_to_mac(vki.ASCII);
                    if (mac_code != 0xFF) {
                        umac_kbd_event((mac_code << 1) | 1, vki.down);
                    }
                    if (vki.vk == fabgl::VK_LSHIFT || vki.vk == fabgl::VK_RSHIFT) {
                        umac_kbd_event((0x38 << 1) | 1, vki.down);
                    } else if (vki.vk == fabgl::VK_LALT || vki.vk == fabgl::VK_RALT) {
                        umac_kbd_event((0x3A << 1) | 1, vki.down);
                    } else if (vki.vk == fabgl::VK_LCTRL || vki.vk == fabgl::VK_RCTRL) {
                        umac_kbd_event((0x37 << 1) | 1, vki.down);
                    }
                }
            }


            taskYIELD();
        }
    }
}

#if BUILD_TARGET == BUILD_TARGET_BOOTLOADER
// Hand control back to ESP32_Bootloader on the next power-up.
//
// The bootloader flashes us into `ota_0` and sets `otadata` to boot it, which
// means the ESP32 would otherwise come straight back here and the bootloader
// menu would be unreachable. Wiping `otadata` makes the ROM fall back to the
// `factory` partition (the bootloader) next time.
//
// Harmless in a standalone flash layout such as `huge_app`: there is no
// `otadata` partition to find, so this is a no-op.
static void bootloader_release_otadata(void) {
    const esp_partition_t *otadata = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_OTA, NULL);
    if (!otadata) {
        return;
    }
    esp_partition_erase_range(otadata, 0, otadata->size);
}
#endif

void setup() {
#if BUILD_TARGET == BUILD_TARGET_BOOTLOADER
    // Must run before anything else, per the ESP32_Bootloader integration notes.
    bootloader_release_otadata();
#endif

    Serial.begin(115200);
    delay(1000); // Give serial time to connect
    Serial.println("\n\n>>> TTGOVGA32INTOSH STARTING UP <<<");
    Serial.println(">>> [1/5] Initializing Display...");

    ESP_LOGI(TAG, "TTGOVGA32intosh starting...");

    // PS2 uses standard TTGO VGA32 pins (KB: 26, 27 / Mouse: 32, 33)
    PS2Controller.begin(fabgl::PS2Preset::KeyboardPort0_MousePort1);

    display_task_start(0, 2);

    Serial.println(">>> [2/5] Initializing UMAC Task...");
    xTaskCreatePinnedToCore(umac_task, "umac", 32768, NULL, 5, NULL, 1);

    Serial.println(">>> [3/5] Initializing PS/2 Peripherals...");
}

void loop() {
    vTaskDelay(pdMS_TO_TICKS(1000));
}
