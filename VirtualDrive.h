#ifndef VIRTUAL_DRIVE_H
#define VIRTUAL_DRIVE_H

// Read-only FAT12 volume generated on the fly, holding a single file.
// Geometry matches `newfs_msdos -F 12 -f 1440` (a 1.44 MB floppy) with no
// partition table, which macOS, Windows and Linux all mount as a superfloppy.
// Pure C++ so the host test in test/ can build the same image.

#include <stdint.h>
#include <string.h>

#define VDRIVE_SECTOR_SIZE     512
#define VDRIVE_SECTOR_COUNT    2880
#define VDRIVE_RESERVED        1
#define VDRIVE_FAT_COUNT       2
#define VDRIVE_FAT_SECTORS     9
#define VDRIVE_ROOT_ENTRIES    224
#define VDRIVE_ROOT_SECTORS    (VDRIVE_ROOT_ENTRIES * 32 / VDRIVE_SECTOR_SIZE)
#define VDRIVE_FAT_START       VDRIVE_RESERVED
#define VDRIVE_ROOT_START      (VDRIVE_FAT_START + VDRIVE_FAT_COUNT * VDRIVE_FAT_SECTORS)
#define VDRIVE_DATA_START      (VDRIVE_ROOT_START + VDRIVE_ROOT_SECTORS)
#define VDRIVE_MAX_FILE_BYTES  ((uint32_t)(VDRIVE_SECTOR_COUNT - VDRIVE_DATA_START) * VDRIVE_SECTOR_SIZE)

#define VDRIVE_LABEL           "ADASPACE   "
#define VDRIVE_FILE_NAME       "CONFIG  HTM"
// FAT date for 2026-01-01: (year - 1980) << 9 | month << 5 | day
#define VDRIVE_FILE_DATE       (((2026 - 1980) << 9) | (1 << 5) | 1)

static inline void vdrive_put16(uint8_t *p, uint16_t v) {
  p[0] = (uint8_t)v;
  p[1] = (uint8_t)(v >> 8);
}

static inline void vdrive_put32(uint8_t *p, uint32_t v) {
  vdrive_put16(p, (uint16_t)v);
  vdrive_put16(p + 2, (uint16_t)(v >> 16));
}

static inline uint32_t vdrive_cluster_count(uint32_t page_len) {
  return (page_len + VDRIVE_SECTOR_SIZE - 1) / VDRIVE_SECTOR_SIZE;
}

static inline uint16_t vdrive_fat_entry(uint32_t k, uint32_t clusters) {
  if (k == 0) return 0xFF0;  // media byte F0 in the low 8 bits
  if (k == 1) return 0xFFF;
  if (k < 2 || k >= 2 + clusters) return 0x000;
  return (k == 1 + clusters) ? 0xFFF : (uint16_t)(k + 1);
}

// FAT12 packs two 12-bit entries into three bytes.
static inline uint8_t vdrive_fat_byte(uint32_t b, uint32_t clusters) {
  uint32_t pair = b / 3;
  uint16_t e0 = vdrive_fat_entry(pair * 2, clusters);
  uint16_t e1 = vdrive_fat_entry(pair * 2 + 1, clusters);
  switch (b % 3) {
    case 0:  return (uint8_t)(e0 & 0xFF);
    case 1:  return (uint8_t)(((e0 >> 8) & 0x0F) | ((e1 & 0x0F) << 4));
    default: return (uint8_t)(e1 >> 4);
  }
}

static inline void vdrive_boot_sector(uint8_t *s) {
  static const uint8_t jump[3] = {0xEB, 0x3C, 0x90};
  memcpy(s, jump, 3);
  memcpy(s + 3, "MSDOS5.0", 8);
  vdrive_put16(s + 11, VDRIVE_SECTOR_SIZE);
  s[13] = 1;                                   // sectors per cluster
  vdrive_put16(s + 14, VDRIVE_RESERVED);
  s[16] = VDRIVE_FAT_COUNT;
  vdrive_put16(s + 17, VDRIVE_ROOT_ENTRIES);
  vdrive_put16(s + 19, VDRIVE_SECTOR_COUNT);
  s[21] = 0xF0;                                // media
  vdrive_put16(s + 22, VDRIVE_FAT_SECTORS);
  vdrive_put16(s + 24, 18);                    // sectors per track
  vdrive_put16(s + 26, 2);                     // heads
  s[38] = 0x29;                                // extended boot signature
  vdrive_put32(s + 39, 0x41443344);            // volume serial
  memcpy(s + 43, VDRIVE_LABEL, 11);
  memcpy(s + 54, "FAT12   ", 8);
  s[510] = 0x55;
  s[511] = 0xAA;
}

static inline void vdrive_root_sector(uint8_t *s, uint32_t page_len) {
  uint8_t *label = s;
  memcpy(label, VDRIVE_LABEL, 11);
  label[11] = 0x08;                            // volume label
  vdrive_put16(label + 24, VDRIVE_FILE_DATE);

  uint8_t *file = s + 32;
  memcpy(file, VDRIVE_FILE_NAME, 11);
  file[11] = 0x21;                             // read-only | archive
  vdrive_put16(file + 16, VDRIVE_FILE_DATE);   // created
  vdrive_put16(file + 18, VDRIVE_FILE_DATE);   // accessed
  vdrive_put16(file + 24, VDRIVE_FILE_DATE);   // modified
  vdrive_put16(file + 26, page_len ? 2 : 0);   // first cluster
  vdrive_put32(file + 28, page_len);
}

// Fills one 512-byte sector of the volume. The caller guarantees
// page_len <= VDRIVE_MAX_FILE_BYTES.
static inline void vdrive_read_sector(uint32_t lba, uint8_t *buf,
                                      const char *page, uint32_t page_len) {
  memset(buf, 0, VDRIVE_SECTOR_SIZE);
  uint32_t clusters = vdrive_cluster_count(page_len);

  if (lba == 0) {
    vdrive_boot_sector(buf);
  } else if (lba < VDRIVE_ROOT_START) {
    uint32_t fat_sector = (lba - VDRIVE_FAT_START) % VDRIVE_FAT_SECTORS;
    uint32_t base = fat_sector * VDRIVE_SECTOR_SIZE;
    for (uint32_t i = 0; i < VDRIVE_SECTOR_SIZE; i++) {
      buf[i] = vdrive_fat_byte(base + i, clusters);
    }
  } else if (lba == VDRIVE_ROOT_START) {
    vdrive_root_sector(buf, page_len);
  } else if (lba >= VDRIVE_DATA_START && lba < VDRIVE_SECTOR_COUNT) {
    uint32_t offset = (lba - VDRIVE_DATA_START) * VDRIVE_SECTOR_SIZE;
    if (offset < page_len) {
      uint32_t n = page_len - offset;
      if (n > VDRIVE_SECTOR_SIZE) n = VDRIVE_SECTOR_SIZE;
      memcpy(buf, page + offset, n);
    }
  }
}

#endif // VIRTUAL_DRIVE_H
