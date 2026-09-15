/* Host test for sm2235.c: record the bit-banged waveform and decode it like the chip would.
 * Build: gcc -std=gnu99 -Wall -Istub -I../src test_sm2235.c ../src/sm2235.c -o test_sm2235 */
#include <stdio.h>
#include <stdlib.h>
#include "tl_common.h"
#include "sm2235.h"

#define SCL 0x110
#define SDA 0x203

static int scl = 1, sda_out = 1, sda_oe = 1;
static int line_sda(void) { return sda_oe ? sda_out : 1; } /* released line is pulled up */

/* decoder state */
static uint8_t frames[16][16];
static int frame_len[16];
static int nframes = 0;
static int in_frame = 0, bitcount = 0, cur_byte = 0, prev_scl = 1, prev_sda = 1;
static int errors = 0;
static int clock_rises_while_sda_changed = 0;
static int irq_enabled = 1, irq_depth_errors = 0;
static int unmasked_rises_in_frame = 0; /* only each frame's STOP clock may rise with IRQs enabled */

static void sample(void) {
    int s = scl, d = line_sda();
    if (s && prev_scl && d != prev_sda) {
        if (!d) { /* START */
            in_frame = 1; bitcount = 0; cur_byte = 0; frame_len[nframes] = 0;
        } else if (in_frame) { /* STOP */
            /* The STOP's own clock rise registers as one sampled bit; anything else is mid-byte. */
            if (bitcount != 1) { printf("  STOP mid-byte (bit %d)\n", bitcount); errors++; }
            in_frame = 0; nframes++;
        }
    }
    if (in_frame && s && !prev_scl && irq_enabled) unmasked_rises_in_frame++;
    if (in_frame && s && !prev_scl) { /* rising edge: sample */
        if (bitcount < 8) {
            cur_byte = (cur_byte << 1) | d;
        } else { /* 9th clock = ACK slot, DATA must be released */
            if (sda_oe) { printf("  ACK slot with DATA still driven\n"); errors++; }
            frames[nframes][frame_len[nframes]++] = (uint8_t)cur_byte;
            cur_byte = 0;
        }
        bitcount = (bitcount + 1) % 9;
    }
    prev_scl = s; prev_sda = d;
}

void gpio_write(uint32_t pin, unsigned v) {
    if (pin == SCL) scl = v ? 1 : 0;
    else if (pin == SDA) {
        int nv = v ? 1 : 0;
        int is_stop = scl && in_frame && bitcount == 1 && nv == 1 && sda_out == 0;
        if (scl && sda_oe && in_frame && nv != sda_out && !is_stop) clock_rises_while_sda_changed++;
        sda_out = nv;
    }
    sample();
}
void gpio_set_output_en(uint32_t pin, unsigned v) { if (pin == SDA) { sda_oe = v ? 1 : 0; sample(); } }
void gpio_set_input_en(uint32_t pin, unsigned v) { (void)pin; (void)v; }
void gpio_set_func(uint32_t pin, GPIO_FuncTypeDef f) { (void)pin; (void)f; }
void sleep_us(unsigned long us) { (void)us; }
u32 drv_disable_irq(void) { u32 was = (u32)irq_enabled; irq_enabled = 0; return was; }
u32 drv_restore_irq(u32 en) { if (irq_enabled) irq_depth_errors++; irq_enabled = (int)en; return en; }

static void expect_frame(int idx, const uint8_t *exp, int len, const char *what) {
    if (idx >= nframes) { printf("FAIL %s: missing frame %d\n", what, idx); errors++; return; }
    int ok = frame_len[idx] == len && memcmp(frames[idx], exp, len) == 0;
    printf("%s %s:", ok ? "ok  " : "FAIL", what);
    for (int i = 0; i < frame_len[idx]; i++) printf(" %02x", frames[idx][i]);
    printf("\n");
    if (!ok) errors++;
}

static void reset_capture(void) { nframes = 0; memset(frame_len, 0, sizeof(frame_len)); }

int main(void) {
    static const uint8_t zero_all[12] = {0xD8, 0x33, 0,0, 0,0, 0,0, 0,0, 0,0};
    static const uint8_t standby[12]  = {0xC0, 0x00, 0,0, 0,0, 0,0, 0,0, 0,0};

    sm2235_init(SCL, SDA, 3, 3);
    printf("init -> %d frames\n", nframes);
    expect_frame(0, zero_all, 12, "init clear (0xD8, cur 3/3)");
    expect_frame(1, standby, 12, "init standby (0xC0)");

    reset_capture();
    uint16_t cw[5] = {0, 0, 0, 1023, 512};
    sm2235_set(cw);
    const uint8_t exp_cw[12] = {0xD0, 0x03, 0,0, 0,0, 0,0, 0x03,0xFF, 0x02,0x00};
    expect_frame(0, exp_cw, 12, "CW only (0xD0, cw nibble only)");

    reset_capture();
    int sent = sm2235_set(cw);
    printf("%s unchanged values send nothing (%d frames, returned %d)\n", nframes == 0 && !sent ? "ok  " : "FAIL", nframes, sent);
    if (nframes || sent) errors++;

    reset_capture();
    sm2235_invalidate();
    sent = sm2235_set(cw);
    printf("%s after invalidate, unchanged values are sent again (returned %d)\n", sent ? "ok  " : "FAIL", sent);
    if (!sent) errors++;
    expect_frame(0, exp_cw, 12, "invalidate resend");

    reset_capture();
    int lit = sm2235_refresh();
    printf("%s refresh while lit returns %d\n", lit ? "ok  " : "FAIL", lit);
    if (!lit) errors++;
    expect_frame(0, exp_cw, 12, "refresh re-sends the lit frame");
    if (nframes != 1) { printf("FAIL refresh sent %d frames\n", nframes); errors++; }

    reset_capture();
    uint16_t rgb[5] = {1, 300, 1023, 0, 0};
    sm2235_set(rgb);
    const uint8_t exp_rgb[12] = {0xC8, 0x30, 0x00,0x01, 0x01,0x2C, 0x03,0xFF, 0,0, 0,0};
    expect_frame(0, exp_rgb, 12, "RGB only (0xC8, rgb nibble only)");

    reset_capture();
    uint16_t all[5] = {5, 6, 7, 8, 2000};
    sm2235_set(all);
    const uint8_t exp_all[12] = {0xD8, 0x33, 0,5, 0,6, 0,7, 0,8, 0x03,0xFF};
    expect_frame(0, exp_all, 12, "all channels, >1023 clamped");

    reset_capture();
    uint16_t off[5] = {0};
    sm2235_set(off);
    expect_frame(0, zero_all, 12, "off: clear");
    expect_frame(1, standby, 12, "off: standby");

    reset_capture();
    lit = sm2235_refresh();
    printf("%s refresh while off returns %d\n", !lit ? "ok  " : "FAIL", lit);
    if (lit) errors++;
    expect_frame(0, zero_all, 12, "refresh while off: clear");
    expect_frame(1, standby, 12, "refresh while off: standby");

    printf("bus idle high after frames: %s\n", (scl && line_sda()) ? "ok" : "FAIL");
    if (!(scl && line_sda())) errors++;
    printf("DATA changes while CLK high inside bytes: %d\n", clock_rises_while_sda_changed);
    if (clock_rises_while_sda_changed) errors++;

    /* Every data/ACK clock must happen with IRQs masked; only STOP clocks (one per frame) may not. */
    reset_capture();
    unmasked_rises_in_frame = 0;
    sm2235_set(all);
    sm2235_set(off);
    printf("%s clocks with IRQs enabled: %d (expected %d STOP clocks), unbalanced restores: %d, IRQs %s after\n",
           unmasked_rises_in_frame == nframes && !irq_depth_errors && irq_enabled ? "ok  " : "FAIL",
           unmasked_rises_in_frame, nframes, irq_depth_errors, irq_enabled ? "enabled" : "DISABLED");
    if (unmasked_rises_in_frame != nframes || irq_depth_errors || !irq_enabled) errors++;

    sm2235_init(0, SDA, 3, 3);
    reset_capture();
    sent = sm2235_set(all);
    lit = sm2235_refresh();
    printf("%s disabled driver (PIN_NC) sends nothing\n", nframes == 0 && !sent && !lit ? "ok  " : "FAIL");
    if (nframes || sent || lit) errors++;

    printf(errors ? "\n%d FAILURE(S)\n" : "\nALL PASSED\n", errors);
    return errors ? 1 : 0;
}
