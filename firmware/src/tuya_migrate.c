#include "tuya_migrate.h"
#include "chip_8258/flash.h"
#include "chip_8258/watchdog.h"
#include "drivers/drv_flash.h"

#define TLNK_FLAG_OFFSET     0x08
#define TLNK_FLAG            0x544C4E4B /* "KNLT" as little-endian u32 */
#define IMAGE_SIZE_OFFSET    0x18
#define OTA_MARKER_OFFSET    0x06
#define OTA_MARKER           0x025D

#define TUYA_APP_ADDR        0x08000
#define TARGET_SLOT_ADDR     0x40000
/* Source [0x8000, 0x8000+size) must not overlap the target slot. */
#define MAX_IMAGE_SIZE       (TARGET_SLOT_ADDR - TUYA_APP_ADDR)
#define MIN_IMAGE_SIZE       0x4000

#define SECTOR_SIZE          0x1000
#define PAGE_LEN             256

/* First word of the Tuya bootloader (identical across Tuya factory dumps). */
#define TUYA_BL_WORD0        0x00008041

/* 512K-layout areas this firmware owns (see drv_nv.h) that must start blank. */
#define WIPE1_START          0x34000 /* NV_BASE_ADDRESS */
#define WIPE1_END            0x40000
#define WIPE2_START          0x78000 /* CFG_PRE_INSTALL_CODE, CFG_FACTORY_RST_CNT, NV_BASE_ADDRESS2 */
#define WIPE2_END            0x80000

/* RAM-resident SPI flash primitives from the SDK (platform/chip_8258/flash.c).
 * The public flash_read_page() etc. are flash-resident, so they cannot run while
 * this image is still at 0x8000 (it is linked for 0x0). */
_attribute_ram_code_sec_noinline_ void flash_mspi_read_ram(unsigned char cmd, unsigned long addr,
                                                           unsigned char addr_en, unsigned char dummy_cnt,
                                                           unsigned char *data, unsigned long data_len);
_attribute_ram_code_sec_noinline_ unsigned char flash_mspi_write_ram(unsigned char cmd, unsigned long addr,
                                                                     unsigned char addr_en, unsigned char *data,
                                                                     unsigned long data_len);

_attribute_ram_code_sec_noinline_ static void rc_read(u32 addr, u32 len, u8 *buf) {
    flash_mspi_read_ram(FLASH_READ_CMD, addr, 1, 0, buf, len);
}

/* Callers pass page-aligned addresses and len <= PAGE_LEN. */
_attribute_ram_code_sec_noinline_ static void rc_write(u32 addr, u32 len, u8 *buf) {
    flash_mspi_write_ram(FLASH_WRITE_CMD, addr, 1, buf, len);
}

_attribute_ram_code_sec_noinline_ static void rc_erase_sector(u32 addr) {
    flash_mspi_write_ram(FLASH_SECT_ERASE_CMD, addr, 1, 0, 0);
}

_attribute_ram_code_sec_noinline_ static u32 rc_read_u32(u32 addr) {
    u32 v = 0;
    rc_read(addr, 4, (u8 *)&v);
    return v;
}

_attribute_ram_code_sec_noinline_ static void rc_clear_u32(u32 addr) {
    u8 zero[4] = {0, 0, 0, 0};
    rc_write(addr, 4, zero);
}

_attribute_ram_code_sec_noinline_ static void rc_reset(void) {
    SYSTEM_RESET();
    while (1) {
    }
}

_attribute_ram_code_sec_noinline_ static void rc_unprotect(void) {
    u8 status = 0;
    flash_mspi_write_ram(FLASH_WRITE_STATUS_CMD_LOWBYTE, 0, 0, &status, 1);
}

_attribute_ram_code_sec_noinline_ void tuya_migrate_stage1(void) {
    /* First test with a memory-mapped read only (same test as romasku's is_bootloader_mode()).
     * SPI flash commands call sleep_us(), which hangs until drv_platform_init() has started the
     * system timer, so they must not run on a normal boot.
     * - Launched by the Tuya bootloader: it copies our RAM code (header included) to SRAM and
     *   soft-resets into it, so this address reads 'KNLT'. CPU address 0x0 then shows our own
     *   RAM code, NOT the bootloader: never test *(u32*)0x0 here (1.0.09 did, and hung on a
     *   real Tuya module because of it).
     * - Booted by the ROM from 0x0 or 0x40000: this reads our own image at offset 0x8008,
     *   which is code, not 'KNLT' (tools/make_ota.py refuses images where it is). */
    if (*(u32 *)(TUYA_APP_ADDR + TLNK_FLAG_OFFSET) != TLNK_FLAG) {
        return; /* not installed by the Tuya bootloader */
    }

    /* The Tuya bootloader leaves the system timer running, so SPI reads of physical flash are
     * safe from here (1.0.01's stage 1 used them in this context). Confirm the layout. */
    if (rc_read_u32(0x0) != TUYA_BL_WORD0 || rc_read_u32(TUYA_APP_ADDR + TLNK_FLAG_OFFSET) != TLNK_FLAG) {
        rc_reset(); /* flash code can't run from 0x8000, so returning would hang: let the bootloader retry */
    }

    u8 marker[2];
    rc_read(TUYA_APP_ADDR + OTA_MARKER_OFFSET, 2, marker);
    u32 size = rc_read_u32(TUYA_APP_ADDR + IMAGE_SIZE_OFFSET);
    if ((marker[0] | (marker[1] << 8)) != OTA_MARKER || size < MIN_IMAGE_SIZE || size > MAX_IMAGE_SIZE) {
        rc_reset(); /* flash code can't run from 0x8000; let the bootloader retry */
    }

    rc_unprotect();

    u8 buf[PAGE_LEN];
    u8 chk[PAGE_LEN];
    for (u32 off = 0; off < size; off += PAGE_LEN) {
        wd_clear();
        if ((off & (SECTOR_SIZE - 1)) == 0) {
            rc_erase_sector(TARGET_SLOT_ADDR + off);
        }
        rc_read(TUYA_APP_ADDR + off, PAGE_LEN, buf);
        rc_write(TARGET_SLOT_ADDR + off, PAGE_LEN, buf);
        rc_read(TARGET_SLOT_ADDR + off, PAGE_LEN, chk);
        for (u32 i = 0; i < PAGE_LEN; i++) {
            if (buf[i] != chk[i]) {
                rc_reset(); /* boot flags untouched: next boot repeats the copy */
            }
        }
    }

    /* Copy verified. Hand boot over to 0x40000 first, then disarm the 0x8000
     * copy so stage 1 never re-triggers. */
    rc_clear_u32(0x0 + TLNK_FLAG_OFFSET);
    rc_clear_u32(TUYA_APP_ADDR + TLNK_FLAG_OFFSET);
    rc_reset();
}

/* Must be called after drv_platform_init() (system timer running) and before
 * anything reads NV. */
void tuya_migrate_stage2(void) {
    /* Pending wipe: Tuya bootloader bytes still at physical 0x0 with both boot
     * flags cleared by stage 1. SPI reads use physical addresses (no remap). */
    if (rc_read_u32(0x0) != TUYA_BL_WORD0 ||
        rc_read_u32(0x0 + TLNK_FLAG_OFFSET) != 0 ||
        rc_read_u32(TUYA_APP_ADDR + TLNK_FLAG_OFFSET) != 0) {
        return;
    }

    flash_unlock();
    for (u32 a = WIPE1_START; a < WIPE1_END; a += SECTOR_SIZE) {
        wd_clear();
        rc_erase_sector(a);
    }
    for (u32 a = WIPE2_START; a < WIPE2_END; a += SECTOR_SIZE) {
        wd_clear();
        rc_erase_sector(a);
    }
    /* Record completion by erasing the disarmed bootloader sector. */
    rc_erase_sector(0x0);
    flash_lock();
}
