#include "display.h"
#include "user_config.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

#include "fabgl.h"

extern fabgl::VGA2Controller DisplayController;

static const char *TAG = "display";

#ifndef DISP_WIDTH
#define DISP_WIDTH 512
#endif
#ifndef DISP_HEIGHT
#define DISP_HEIGHT 342
#endif

#define FB_STRIDE (DISP_WIDTH / 8)  // 64 bytes per row

static TaskHandle_t display_task_handle = NULL;
static const uint8_t *current_framebuffer = NULL;
static volatile bool frame_pending = false;

void display_init(void) {
    // No longer need a separate fb_copy buffer
}

void display_notify_update(void) {
    if (display_task_handle) {
        frame_pending = true;
        xTaskNotifyGive(display_task_handle);
    }
}

static void display_task(void *arg) {
    ESP_LOGI(TAG, "Display task started on Core %d", xPortGetCoreID());

    // Initialize VGA 640x480 @ 60Hz
    DisplayController.begin();
    DisplayController.setResolution(VGA_640x480_60Hz);

    // Suspend the background primitive execution task - we write directly to scanlines
    DisplayController.suspendBackgroundPrimitiveExecution();

    // Set palette: index 0 = white (Mac's 0 bit), index 1 = black (Mac's 1 bit)
    DisplayController.setPaletteItem(0, fabgl::RGB888(255, 255, 255));
    DisplayController.setPaletteItem(1, fabgl::RGB888(0, 0, 0));

    // Get viewport dimensions
    int vpWidth = DisplayController.getViewPortWidth();
    int vpHeight = DisplayController.getViewPortHeight();
    printf(">>> [DISPLAY] VGA viewport: %dx%d\n", vpWidth, vpHeight);

    // Calculate centering offsets in pixels
    int offsetX = (vpWidth - DISP_WIDTH) / 2;
    int offsetY = (vpHeight - DISP_HEIGHT) / 2;
    printf(">>> [DISPLAY] Mac screen offset: x=%d, y=%d\n", offsetX, offsetY);

    // Clear entire screen to black (palette index 1)
    // In VGA2: bit=1 means palette index 1 = black, so 0xFF = 8 black pixels per byte.
    for (int y = 0; y < vpHeight; y++) {
        uint8_t *row = DisplayController.getScanline(y);
        memset(row, 0xFF, vpWidth / 8);  // All pixels = palette index 1 (black)
    }

    int frame_count = 0;
    while (1) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        if (frame_pending && current_framebuffer) {
            frame_count++;
            frame_pending = false;

            // Direct framebuffer copy!
            // Mac FB: 1 bit per pixel, MSB first, 1=black, 0=white
            // VGA2 FB: 1 bit per pixel, MSB first, 1=palette[1]=black, 0=palette[0]=white
            // They are the same format! We can memcpy row by row.
            
            int offsetBytes = offsetX / 8;  // Convert pixel offset to byte offset
            
            for (int y = 0; y < DISP_HEIGHT; y++) {
                uint8_t *vgaRow = DisplayController.getScanline(y + offsetY);
                const uint8_t *macRow = current_framebuffer + (y * FB_STRIDE);
                memcpy(vgaRow + offsetBytes, macRow, FB_STRIDE);
            }

            if (frame_count == 1) {
                // printf(">>> [DISPLAY] First frame received! Drawing to screen...\n");
                // printf(">>> [DISPLAY] First frame drawn successfully!\n");
            }
            if (frame_count % 60 == 0) {
                // printf(">>> [DISPLAY] Drawn %d frames.\n", frame_count);
            }
        }
    }
}

void display_task_start(int core, int priority) {
    display_init();
    if (display_task_handle != NULL) return;
    xTaskCreatePinnedToCore(display_task, "display_task", 4096, NULL, priority, &display_task_handle, core);
}

void display_set_framebuffer(const uint8_t *fb) {
    current_framebuffer = fb;
}