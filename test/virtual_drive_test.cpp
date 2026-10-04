// Writes the config-mode drive image and the raw page bytes so run.sh can
// mount the image and compare. Usage: virtual_drive_test <image> <page>
#include <stdio.h>

#include "../ConfigPage.h"
#include "../VirtualDrive.h"

static_assert(sizeof(CONFIG_PAGE) - 1 <= VDRIVE_MAX_FILE_BYTES, "config page does not fit the virtual drive");

int main(int argc, char **argv) {
  if (argc != 3) {
    fprintf(stderr, "usage: %s <image> <page>\n", argv[0]);
    return 2;
  }

  FILE *img = fopen(argv[1], "wb");
  if (!img) { perror(argv[1]); return 1; }
  uint8_t sector[VDRIVE_SECTOR_SIZE];
  for (uint32_t lba = 0; lba < VDRIVE_SECTOR_COUNT; lba++) {
    vdrive_read_sector(lba, sector, CONFIG_PAGE, CONFIG_PAGE_LEN);
    if (fwrite(sector, 1, sizeof(sector), img) != sizeof(sector)) { perror(argv[1]); return 1; }
  }
  fclose(img);

  FILE *page = fopen(argv[2], "wb");
  if (!page) { perror(argv[2]); return 1; }
  if (fwrite(CONFIG_PAGE, 1, CONFIG_PAGE_LEN, page) != CONFIG_PAGE_LEN) { perror(argv[2]); return 1; }
  fclose(page);
  return 0;
}
