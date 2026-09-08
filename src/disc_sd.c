#include "disc_sd.h"
// TTGO VGA32 V1.4 SD Card Pins
#define SD_MOSI 12
#define SD_MISO 2
#define SD_CLK 14
#define SD_CS 13
#define SD_MOUNT_POINT "/sdcard"

#include "driver/spi_common.h"
#include "driver/sdspi_host.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"

#include <stdio.h>
#include <string.h>

static const char *TAG = "disc_sd";

static sdmmc_card_t *s_card = NULL;
static bool s_mounted = false;

/* ── shared file I/O callbacks (same pattern as disc_lfs.c) ───────────── */

static int disc_sd_read(void *ctx, uint8_t *data, unsigned int offset,
                        unsigned int len) {
    FILE *f = (FILE *)ctx;
    if (!f) {
        ESP_LOGE(TAG, "read: file is NULL");
        return -1;
    }
    if (fseek(f, offset, SEEK_SET) != 0) {
        ESP_LOGE(TAG, "read: fseek failed at offset=%u", offset);
        return -1;
    }
    size_t got = fread(data, 1, len, f);
    if (got != len) {
        ESP_LOGE(TAG, "read ERROR: offset=%u len=%u got=%u (ferror=%d feof=%d)",
                 offset, len, (unsigned)got, ferror(f), feof(f));
        return -1;
    }
    ESP_LOGD(TAG, "read OK: offset=0x%05x len=%u", offset, len);
    return 0;
}

static int disc_sd_write(void *ctx, uint8_t *data, unsigned int offset,
                         unsigned int len) {
    FILE *f = (FILE *)ctx;
    if (!f) {
        ESP_LOGE(TAG, "write: file is NULL");
        return -1;
    }
    if (fseek(f, offset, SEEK_SET) != 0) {
        ESP_LOGE(TAG, "write: fseek failed at offset=%u", offset);
        return -1;
    }
    size_t written = fwrite(data, 1, len, f);
    fflush(f);
    if (written != len) {
        ESP_LOGE(TAG, "write ERROR: offset=%u len=%u wrote=%u (ferror=%d)",
                 offset, len, (unsigned)written, ferror(f));
        return -1;
    }
    ESP_LOGD(TAG, "write OK: offset=0x%05x len=%u", offset, len);
    return 0;
}

/* ── public API ───────────────────────────────────────────────────────── */

int disc_sd_open(disc_descr_t *disc, const char *filename, int read_only) {
    ESP_LOGI(TAG, "Mounting SD card on SPI3_HOST (MOSI=%d MISO=%d CLK=%d CS=%d)",
             SD_MOSI, SD_MISO, SD_CLK, SD_CS);

    /* SPI3_HOST (VSPI) is free — touch now uses bit-bang GPIO SPI.
     * Initialize the bus with the SD card's native VSPI pins. */
    spi_bus_config_t bus_cfg = {
        .mosi_io_num     = SD_MOSI,   // GPIO 23
        .miso_io_num     = SD_MISO,   // GPIO 19
        .sclk_io_num     = SD_CLK,    // GPIO 18
        .quadwp_io_num   = -1,
        .quadhd_io_num   = -1,
        .max_transfer_sz = 4096,
    };
    esp_err_t ret = spi_bus_initialize(SPI3_HOST, &bus_cfg, SPI_DMA_CH2);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "SPI3 bus init failed: %s", esp_err_to_name(ret));
        return -1;
    }

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SPI3_HOST;

    sdspi_device_config_t slot_cfg = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_cfg.gpio_cs = SD_CS;     // GPIO 5
    slot_cfg.gpio_cd = SDSPI_SLOT_NO_CD;
    slot_cfg.gpio_wp = SDSPI_SLOT_NO_WP;
    slot_cfg.host_id = SPI3_HOST;

    esp_vfs_fat_sdmmc_mount_config_t mount_cfg = {
        .format_if_mount_failed = false,
        .max_files              = 4,
        .allocation_unit_size   = 16 * 1024,
    };

    printf(">>> [SD] Mounting SD Card...\n");
    ret = esp_vfs_fat_sdspi_mount(SD_MOUNT_POINT, &host, &slot_cfg,
                                  &mount_cfg, &s_card);
    if (ret != ESP_OK) {
        printf(">>> [SD] ERROR: SD mount failed: %s\n", esp_err_to_name(ret));
        ESP_LOGE(TAG, "SD mount failed: %s", esp_err_to_name(ret));
        spi_bus_free(SPI3_HOST);
        return -1;
    }
    s_mounted = true;
    printf(">>> [SD] SD Card Mounted successfully!\n");

    sdmmc_card_print_info(stdout, s_card);

    /* Open the disk image file */
    char path[64];
    snprintf(path, sizeof(path), "%s/%s", SD_MOUNT_POINT, filename);
    printf(">>> [SD] Opening disk image: %s\n", path);

    FILE *f = fopen(path, read_only ? "rb" : "r+b");
    if (!f && !read_only) {
        /* Try creating the file if it doesn't exist yet */
        printf(">>> [SD] File not found. Creating %s...\n", path);
        f = fopen(path, "wb");
        if (f) {
            fclose(f);
            f = fopen(path, "r+b");
        }
    }
    if (!f) {
        printf(">>> [SD] ERROR: Failed to open %s\n", path);
        ESP_LOGE(TAG, "Failed to open %s (read_only=%d)", path, read_only);
        esp_vfs_fat_sdcard_unmount(SD_MOUNT_POINT, s_card);
        spi_bus_free(SPI3_HOST);
        s_mounted = false;
        s_card = NULL;
        return -1;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    /* Dump first 8 bytes to confirm disk content */
    uint8_t hdr[8] = {0};
    fread(hdr, 1, sizeof(hdr), f);
    fseek(f, 0, SEEK_SET);
    
    printf(">>> [SD] Opened %s successfully! Size: %ld bytes\n", path, size);
    
    ESP_LOGI(TAG, "Opened %s  size=%ld  [%02x %02x %02x %02x %02x %02x %02x %02x]",
             path, size,
             hdr[0], hdr[1], hdr[2], hdr[3],
             hdr[4], hdr[5], hdr[6], hdr[7]);

    disc->op_ctx   = f;
    disc->size     = (unsigned int)size;
    disc->read_only = read_only;
    disc->base     = NULL;
    disc->op_read  = disc_sd_read;
    disc->op_write = read_only ? NULL : disc_sd_write;

    return 0;
}

void disc_sd_deinit(void) {
    if (s_mounted && s_card) {
        esp_vfs_fat_sdcard_unmount(SD_MOUNT_POINT, s_card);
        spi_bus_free(SPI3_HOST);
        s_card    = NULL;
        s_mounted = false;
        ESP_LOGI(TAG, "SD card unmounted");
    }
}
