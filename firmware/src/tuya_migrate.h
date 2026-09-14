#pragma once
#include "tl_common.h"

/*
 * Migration from the Tuya stock layout (Telink-style bootloader at 0x0,
 * application at 0x8000) to the SDK "no bootloader" dual-slot layout
 * (image at 0x0 or 0x40000) used by this firmware.
 *
 * tuya_migrate_stage1(): runs from RAM before anything else. If this image
 * is sitting at 0x8000 (the Tuya bootloader just installed it via OTA), copy it
 * to 0x40000 with per-page verification, then invalidate the Tuya
 * bootloader and the 0x8000 copy so the ROM boots 0x40000, and reset.
 * Power loss at any point before the flags are cleared just repeats the copy.
 *
 * tuya_migrate_stage2(): call right after drv_platform_init(). On the first
 * boot from 0x40000 it erases the NV and config areas this firmware uses (they
 * still contain stock-app bytes or a staged OTA copy), then erases sector 0 to
 * record completion.
 *
 * Lesson from the first test: SPI flash helpers call sleep_us(), which hangs
 * until the system timer is started by drv_platform_init(). Stage 1 therefore
 * detects via memory-mapped reads, and stage 2 runs after platform init.
 */
void tuya_migrate_stage1(void);
void tuya_migrate_stage2(void);
