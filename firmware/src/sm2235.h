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

/* Set OUT1..OUT5 (0..1023). Sends only when values change (or after sm2235_invalidate()); all zero
 * enters standby. Returns non-zero if a frame was sent. There is no acknowledgement, so callers should
 * follow a change with sm2235_refresh() shortly afterwards and periodically while lit. */
int sm2235_set(const uint16_t out[SM2235_CHANNELS]);

/* Forget what was last sent, so the next sm2235_set() transmits even if the values are unchanged. */
void sm2235_invalidate(void);

/* Re-send the last state: the output frame if lit, otherwise clear + standby.
 * Recovers from a lost or corrupted frame, a frame swallowed while the chip wakes from standby, or an
 * LED driver reset. Returns non-zero if the last state is lit. */
int sm2235_refresh(void);
