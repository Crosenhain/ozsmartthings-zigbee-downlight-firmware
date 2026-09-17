/* Host test for light_control.c: on/off and level state machine against a simulated timer wheel.
 * Checks that the OnOff attribute, CurrentLevel and the LED output agree after each command sequence.
 * Build: gcc -std=gnu99 -Wall -Istub -I../src test_light_control.c ../src/light_control.c ../src/color_math.c -lm -o test_light_control */
#include <stdio.h>
#include <stdlib.h>
#include "tl_common.h"
#include "zcl_include.h"
#include "app.h"
#include "light_control.h"
#include "hw_config.h"
#include "sm2235.h"

AppCtx light_ctx;
ZclSceneAttr zcl_scene_attrs;
ZclOnOffAttr zcl_on_off_attrs;
ZclLevelAttr zcl_level_attrs;
ZclLightColorCtrlAttr zcl_color_ctrl_attrs;

static int errors = 0;

/* ---- timer wheel (1 ms resolution) ---- */
#define NTIMERS 8
static ev_timer_event_t timers[NTIMERS];
static uint32_t now_ms, next_serial;

ev_timer_event_t* sim_timer_schedule(ev_timer_callback_t cb, void* arg, uint32_t ms) {
    for (int i = 0; i < NTIMERS; i++) {
        if (!timers[i].used) {
            timers[i] = (ev_timer_event_t){cb, arg, ms, now_ms + ms, ++next_serial, 1};
            return &timers[i];
        }
    }
    printf("FAIL out of timers\n");
    exit(1);
}

void sim_timer_cancel(ev_timer_event_t** evt) {
    if (*evt) { (*evt)->used = 0; *evt = NULL; }
}

static void run_ms(uint32_t ms) {
    for (uint32_t end = now_ms + ms; now_ms < end;) {
        now_ms++;
        for (int i = 0; i < NTIMERS; i++) {
            ev_timer_event_t* t = &timers[i];
            if (!t->used || t->due > now_ms) continue;
            uint32_t serial = t->serial;
            int r = t->cb(t->arg);
            if (!t->used || t->serial != serial) continue; /* cancelled (and maybe reused) inside the callback */
            if (r < 0) { t->used = 0; continue; }
            if (r > 0) t->interval = (uint32_t)r;
            t->due = now_ms + t->interval;
        }
    }
}

/* ---- hardware stubs ---- */
static HwConfig cfg = {.scl_pin = 1, .sda_pin = 2, .cur_rgb_code = 3, .cur_cw_code = 3, .out_map = {0, 1, 2, 3, 4},
                       .cold_temp_k = 6000, .warm_temp_k = 3000};
HwConfig* hw_config_get(void) { return &cfg; }

static uint16_t chip[SM2235_CHANNELS];
static int went_dark; /* an all-zero frame was sent since this was last cleared */

static int chip_sum(void) { int s = 0; for (int i = 0; i < SM2235_CHANNELS; i++) s += chip[i]; return s; }
void sm2235_init(uint32_t scl, uint32_t sda, uint8_t rgb, uint8_t cw) { memset(chip, 0, sizeof chip); }
int sm2235_set(const uint16_t out[SM2235_CHANNELS]) {
    if (!memcmp(chip, out, sizeof chip)) return 0;
    memcpy(chip, out, sizeof chip);
    if (!chip_sum()) went_dark = 1;
    return 1;
}
void sm2235_invalidate(void) {}
int sm2235_refresh(void) { return chip_sum() != 0; }
void status_led_on(void) {}
void status_led_off(void) {}

/* ---- the upstream callers of light_control.c (zcl_onoff.c, zcl_level.c, zcl_color.c), reduced to what they do to it ---- */
void light_on_off_update(uint8_t cmd) {
    ZclOnOffAttr* p_on_off = ZCL_ONOFF_ATTR_GET();
    if (cmd == ZCL_CMD_ONOFF_TOGGLE) cmd = p_on_off->on_off ? ZCL_CMD_ONOFF_OFF : ZCL_CMD_ONOFF_ON;
    p_on_off->on_off = (cmd == ZCL_CMD_ONOFF_ON) ? ZCL_ONOFF_STATUS_ON : ZCL_ONOFF_STATUS_OFF;
    light_refresh(LIGHT_STA_ON_OFF);
}
void light_on_off_init(void) { light_on_off_update(zcl_on_off_attrs.on_off); }
void light_level_init(void) { zcl_level_attrs.remaining_time = 0; light_refresh(LIGHT_STA_LEVEL); }
void light_color_init(void) { light_refresh(LIGHT_STA_COLOR); }

/* The SDK drops level commands without on/off while the light is off (ExecuteIfOff is clear). */
static void move_to_level(uint8_t level, uint16_t transtime, int with_on_off) {
    if (with_on_off || zcl_on_off_attrs.on_off) light_level_ramp_to_level(level, transtime, with_on_off);
}
static void on(void) { light_on_off_update(ZCL_CMD_ONOFF_ON); }
static void off(void) { light_on_off_update(ZCL_CMD_ONOFF_OFF); }

static void boot(int on_off, uint8_t level) {
    /* the ramp state in light_control.c is static: stop what the last case left running (a Move never ends by itself) */
    light_level_ramp_stop();
    run_ms(1000);
    memset(&light_ctx, 0, sizeof light_ctx);
    zcl_on_off_attrs = (ZclOnOffAttr){.on_off = on_off};
    zcl_level_attrs = (ZclLevelAttr){.cur_level = level, .on_off_transition_time = 3, .min_level = 1, .max_level = 254};
    zcl_color_ctrl_attrs = (ZclLightColorCtrlAttr){.color_mode = ZCL_COLOR_MODE_COLOR_TEMPERATURE_MIREDS,
                                                   .enhanced_color_mode = ZCL_COLOR_MODE_COLOR_TEMPERATURE_MIREDS,
                                                   .color_temperature_mireds = 250,
                                                   .color_temp_physical_min_mireds = 166,
                                                   .color_temp_physical_max_mireds = 333,
                                                   .couple_color_temp_to_level_min_mireds = 166};
    light_init();
    light_load_state();
    run_ms(1000);
}
/* boot lit at `level`, then turn off with a plain Off (which keeps CurrentLevel) */
static void boot_off(uint8_t level) { boot(1, level); off(); run_ms(1000); }

static void expect(const char* what, int want_on, int want_level) {
    int lit = chip_sum() != 0;
    int ok = (zcl_on_off_attrs.on_off == want_on) && (lit == want_on) &&
             (want_level < 0 || zcl_level_attrs.cur_level == want_level);
    printf("%s %-66s OnOff=%u level=%3u LEDs %s\n", ok ? "ok  " : "FAIL", what, zcl_on_off_attrs.on_off,
           zcl_level_attrs.cur_level, lit ? "lit" : "dark");
    if (!ok) errors++;
}
static void check(const char* what, int ok) {
    printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) errors++;
}

int main(void) {
    char b[96];

    printf("plain on/off\n");
    boot(1, 200); expect("boot on at 200", 1, 200);
    off(); run_ms(1000); expect("off keeps CurrentLevel", 0, 200);
    on(); run_ms(1000); expect("on", 1, 200);

    /* Zigbee2MQTT sends {"state":"ON","brightness":N} as a lone MoveToLevelWithOnOff. Off keeps CurrentLevel, so N is
     * usually <= CurrentLevel: 1.0.14 and earlier only turned on when the level rose, and stayed dark. */
    printf("MoveToLevelWithOnOff while off (CurrentLevel 200)\n");
    static const struct { uint8_t level; uint16_t tt; } turn_on[] = {
        {254, 0}, {201, 0}, {200, 0}, {199, 0}, {100, 0}, {2, 0}, {254, 10}, {200, 10}, {100, 10}, {200, 300}};
    for (unsigned i = 0; i < sizeof turn_on / sizeof *turn_on; i++) {
        boot_off(200);
        move_to_level(turn_on[i].level, turn_on[i].tt, 1);
        check("  OnOff is set as the ramp starts", zcl_on_off_attrs.on_off == 1);
        run_ms(turn_on[i].tt * 100 + 1000);
        snprintf(b, sizeof b, "MoveToLevelWithOnOff(%u, %u)", turn_on[i].level, turn_on[i].tt);
        expect(b, 1, turn_on[i].level);
    }

    printf("same brightness every time (Home Assistant toggle with brightness_pct: 100)\n");
    boot_off(127);
    for (int i = 1; i <= 3; i++) {
        move_to_level(254, 0, 1); run_ms(1000);
        snprintf(b, sizeof b, "#%d MoveToLevelWithOnOff(254, 0)", i); expect(b, 1, 254);
        off(); run_ms(1000); expect("   off", 0, 254);
    }

    printf("turn-on ramp fades up from dark instead of popping on at the old level\n");
    boot_off(200);
    move_to_level(200, 20, 1);
    run_ms(200); int early = chip_sum();
    run_ms(3000); int final = chip_sum();
    snprintf(b, sizeof b, "output after 0.2 s of 2 s (%d) is lit and well below the final output (%d)", early, final);
    check(b, early > 0 && early < final / 4);
    expect("MoveToLevelWithOnOff(200, 20)", 1, 200);

    printf("off with a transition (MoveToLevelWithOnOff to 0), then on again with a transition\n");
    static const struct { uint8_t level; uint16_t tt; } slow[] = {{200, 0}, {254, 20}, {50, 20}, {127, 50}, {254, 100}};
    for (unsigned i = 0; i < sizeof slow / sizeof *slow; i++) {
        boot(1, 200);
        move_to_level(0, slow[i].tt, 1); run_ms(slow[i].tt * 100 + 1000);
        expect("MoveToLevelWithOnOff(0) turns off at the minimum level", 0, 1);
        /* CurrentLevel is 1 here and a slow ramp leaves it there for several ticks: that must not turn it back off */
        move_to_level(slow[i].level, slow[i].tt, 1); run_ms(slow[i].tt * 100 + 1000);
        snprintf(b, sizeof b, "then MoveToLevelWithOnOff(%u, %u)", slow[i].level, slow[i].tt);
        expect(b, 1, slow[i].level);
    }

    printf("with on/off, downwards\n");
    boot(1, 2); move_to_level(1, 0, 1); run_ms(1000); expect("on at 2, MoveToLevelWithOnOff(1, 0) turns off", 0, 1);
    boot(1, 200); move_to_level(100, 10, 1); run_ms(2000); expect("on at 200, MoveToLevelWithOnOff(100, 10) stays on", 1, 100);
    boot_off(200); move_to_level(0, 0, 1); run_ms(1000); expect("off, MoveToLevelWithOnOff(0, 0) stays off", 0, 1);

    printf("slow ramps: the remainder is spread in the ramp's direction\n");
    boot(1, 200); move_to_level(201, 300, 1); run_ms(31000); expect("on at 200, MoveToLevelWithOnOff(201, 300)", 1, 201);
    boot(1, 200); move_to_level(199, 300, 0); run_ms(31000); expect("on at 200, MoveToLevel(199, 300)", 1, 199);
    boot(1, 1); move_to_level(254, 150, 1); run_ms(16000); expect("on at 1, MoveToLevelWithOnOff(254, 150)", 1, 254);

    printf("Move with on/off\n");
    boot_off(254); light_level_ramp_at_rate(50, true, true); run_ms(500);
    expect("off at 254, MoveWithOnOff(up, 50): lit, rising from the minimum level", 1, -1);
    check("  CurrentLevel is between the minimum and the old level", zcl_level_attrs.cur_level > 1 && zcl_level_attrs.cur_level < 100);
    run_ms(6000); expect("  ...reaches the maximum", 1, 254);
    boot_off(200); light_level_ramp_at_rate(1, true, true); run_ms(500);
    expect("off, MoveWithOnOff(up, 1): slow start at the minimum level stays on", 1, -1);
    boot(1, 50); light_level_ramp_at_rate(50, false, true); run_ms(2000);
    expect("on at 50, MoveWithOnOff(down, 50) turns off at the minimum level", 0, 1);

    printf("level ramp taking over the 300 ms on/off fade\n");
    boot(1, 200); off(); run_ms(100); went_dark = 0;
    move_to_level(150, 0, 1); run_ms(1000);
    expect("off, 100 ms later MoveToLevelWithOnOff(150, 0)", 1, 150);
    check("  no dark frame in between", !went_dark);
    /* scene recall of a scene that is off: Off, then MoveToLevel straight to the level callback */
    boot(1, 200); off(); light_level_ramp_to_level(150, 0, false); run_ms(1000);
    expect("Off then MoveToLevel(150) in the same instant ends dark", 0, 150);
    on(); run_ms(1000); expect("   on afterwards", 1, 150);

    printf("without on/off\n");
    boot(1, 200); move_to_level(100, 0, 0); run_ms(1000); expect("on, MoveToLevel(100, 0)", 1, 100);
    boot_off(200); move_to_level(100, 0, 0); run_ms(1000); expect("off, MoveToLevel(100, 0) is dropped", 0, 200);

    printf("On right after MoveToLevelWithOnOff (the converter's workaround for 1.0.14 and earlier)\n");
    boot_off(200); move_to_level(200, 0, 1); run_ms(30); on(); run_ms(1000); expect("ramp still running", 1, 200);
    boot_off(200); move_to_level(200, 0, 1); run_ms(500); on(); run_ms(1000); expect("ramp finished", 1, 200);

    printf(errors ? "\n%d FAILURE(S)\n" : "\nALL PASSED\n", errors);
    return errors ? 1 : 0;
}
