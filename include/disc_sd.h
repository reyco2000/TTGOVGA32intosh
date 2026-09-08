#ifndef DISC_SD_H
#define DISC_SD_H

#include "disc.h"

#include <stdint.h>

/**
 * Initialize the SD card over SPI and open a disk image file from it.
 *
 * @param disc      Pointer to disc descriptor to fill in.
 * @param filename  Filename on SD card root (e.g. "disk.img").
 * @param read_only If non-zero, open for reading only.
 * @return 0 on success, -1 on failure (no card, file not found, etc.)
 */
int disc_sd_open(disc_descr_t *disc, const char *filename, int read_only);

/**
 * Unmount the SD card and free resources.
 * Safe to call even if disc_sd_open() failed.
 */
void disc_sd_deinit(void);

#endif
