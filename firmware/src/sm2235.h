#pragma once
#include <stdint.h>
/* No <stdbool.h>: the Telink SDK defines its own bool. */

/*
 * Sunmoon SM2235EGH 5-channel constant-current LED driver, bit-banged 2-wire bus.
 *
 * Frame (MSB first, 9th clock per byte, ACK ignored):
 *   START, byte0 = 110 M1 M0 000 (mode + start address OUT1),
 *   byte1 = RGB current code << 4 | CW current code,
 *   OUT1..OUT5 as 10-bit big-endian words, STOP.
 * Modes: 0xC0 standby, 0xC8 RGB (OUT1-3), 0xD0 CW (OUT4-5), 0xD8 all.
 * Current: RGB 4*(n+1) mA, CW 5*(n+1) mA (datasheet; the ESPHome table is SM2335's).
 */

#define SM2235_CHANNELS  5
#define SM2235_MAX_VALUE 1023
#define SM2235_PIN_NC    0

/* Configure pins and put the chip in standby. Pins == SM2235_PIN_NC disables the driver. */
void sm2235_init(uint32_t scl_pin, uint32_t sda_pin, uint8_t cur_rgb_code, uint8_t cur_cw_code);

/* Set OUT1..OUT5 (0..1023). Sends only when values change; all zero enters standby. */
void sm2235_set(const uint16_t out[SM2235_CHANNELS]);
