/*
 * Modified from nminaylov/zigbee-light-cct (Apache-2.0), 2026: SM2235 output stage replacing PWM, hue/
 * saturation/XY ramps, perceptual colour fades and colour <-> white crossfades. See NOTICE.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tl_common.h"
#include "zcl_include.h"
#include "app.h"
#include "app_ui.h"
#include "light_control.h"
#include "hw_config.h"
#include "sm2235.h"
#include "color_math.h"
#include <math.h>

#define CLAMP(a, min, max) ((a) < (min) ? (min) : ((a) > (max) ? (max) : (a)))

#define ZCL_TRANSITION_INTERVAL       10 //ms, shared brightness/color-temp transition tick (finer = smoother steps)
#define ZCL_TRANSITION_TICKS_PER_UNIT (100 / ZCL_TRANSITION_INTERVAL) //ticks per ZCL 1/10s time unit
#define ZCL_TRANSITION_TICKS_PER_SEC  (1000 / ZCL_TRANSITION_INTERVAL)
#define TRANSITION_TICKS_INFINITE     ((uint32_t)0xFFFFFFFF)

typedef enum {
    TRANSITION_NONE,
    TRANSITION_ONOFF, //on/off cluster fade: ramps effLevel256 only, curLevel/remainingTime untouched
    TRANSITION_LEVEL, //level cluster command ramp: drives curLevel/remainingTime as well as effLevel256
} TransitionMode;

static ev_timer_event_t* transition_timer_evt = NULL;
static TransitionMode transition_mode = TRANSITION_NONE;

//brightness currently pushed to hardware (256x fixed-point)
static uint16_t eff_level_256 = 0;

//on/off fade state
static uint16_t on_off_fade_target_256 = 0;
static int32_t on_off_fade_step_256 = 0;
static uint32_t on_off_fade_remaining = 0;

//level cluster ramp state
static int32_t level_step_256 = 0;
static uint32_t level_remaining_ticks = 0;
static uint32_t level_compensation = 0;
static bool level_with_on_off = false;

//color temperature ramp state; runs independently of the on/off and level ramps above
static int32_t color_temp_step_256 = 0;
static uint32_t color_temp_current_256 = 0;
static uint32_t color_temp_remaining_ticks = 0;
static uint16_t color_temp_min_mireds = 0;
static uint16_t color_temp_max_mireds = 0;

//hue/saturation and XY ramp state (DL41 colour). All values are x256 fixed point.
#define HUE_Q8_WRAP ((int32_t)65536 << 8) //enhanced hue wraps around the colour wheel
#define SAT_MAX     254
#define XY_MAX      0xFEFF
static uint8_t color_ramp_which = 0; //LIGHT_RAMP_* components currently moving
static uint32_t color_remaining_ticks = 0;
static int32_t hue_q8 = 0, hue_step_q8 = 0;
static int32_t sat_q8 = 0, sat_step_q8 = 0;
static int32_t x_q8 = 0, x_step_q8 = 0;
static int32_t y_q8 = 0, y_step_q8 = 0;
static uint16_t color_target_hue = 0;
static uint8_t color_target_sat = 0;
static uint16_t color_target_x = 0;
static uint16_t color_target_y = 0;

//linear RGB (brightest channel 65535, before gains/level) last rendered in a colour mode; all zero = none yet
static uint16_t rgb_shown[3];
//timed XY fades render as a perceptual blend between these colours; blend_total 0 = render straight from x/y
static uint16_t blend_from[3], blend_to[3];
static uint32_t blend_total = 0;

//RGB channel gains in percent: starting point from the stock Tuya config (gmkr/gmkg/gmkb = 80/60/60)
#define RGB_GAIN_R        80
#define RGB_GAIN_G        60
#define RGB_GAIN_B        60
//summed RGB drive cap, in percent of one channel at full (keeps U1 heat and supply load bounded)
#define RGB_SUM_CAP_PCT   200

#define PWM_MAX 8192
// Generate with tools/gamma_lut.py
static const uint16_t gamma_lut[254] = {
    1,    1,    1,    2,    2,    3,    4,    5,    6,    8,    9,    11,   13,   15,   17,   20,   22,   25,   28,
    32,   35,   39,   43,   47,   51,   55,   60,   65,   70,   76,   81,   87,   93,   99,   106,  112,  119,  126,
    134,  141,  149,  157,  166,  174,  183,  192,  201,  211,  220,  230,  241,  251,  262,  273,  284,  295,  307,
    319,  331,  343,  356,  369,  382,  396,  409,  423,  438,  452,  467,  482,  497,  512,  528,  544,  561,  577,
    594,  611,  628,  646,  664,  682,  700,  719,  738,  757,  777,  796,  816,  837,  857,  878,  899,  921,  942,
    964,  986,  1009, 1032, 1055, 1078, 1102, 1125, 1150, 1174, 1199, 1224, 1249, 1275, 1300, 1327, 1353, 1380, 1407,
    1434, 1462, 1489, 1518, 1546, 1575, 1604, 1633, 1662, 1692, 1722, 1753, 1784, 1815, 1846, 1878, 1910, 1942, 1974,
    2007, 2040, 2073, 2107, 2141, 2175, 2210, 2245, 2280, 2315, 2351, 2387, 2424, 2460, 2497, 2534, 2572, 2610, 2648,
    2686, 2725, 2764, 2804, 2843, 2883, 2924, 2964, 3005, 3046, 3088, 3130, 3172, 3214, 3257, 3300, 3343, 3387, 3431,
    3475, 3520, 3565, 3610, 3656, 3701, 3748, 3794, 3841, 3888, 3935, 3983, 4031, 4079, 4128, 4177, 4226, 4276, 4326,
    4376, 4427, 4477, 4529, 4580, 4632, 4684, 4737, 4789, 4842, 4896, 4950, 5004, 5058, 5113, 5168, 5223, 5279, 5335,
    5391, 5448, 5505, 5562, 5619, 5677, 5736, 5794, 5853, 5912, 5972, 6032, 6092, 6152, 6213, 6274, 6336, 6398, 6460,
    6522, 6585, 6648, 6712, 6776, 6840, 6904, 6969, 7034, 7100, 7165, 7231, 7298, 7365, 7432, 7499, 7567, 7635, 7704,
    7772, 7841, 7911, 7981, 8051, 8121, 8192,
};

/* Output stage: SM2235 over bit-banged 2-wire (see sm2235.c). Channel values are
 * kept on the gamma scale (0..PWM_MAX) and converted to 10-bit when pushed. */
static uint16_t out_cold = 0;
static uint16_t out_warm = 0;
static uint16_t out_r = 0;
static uint16_t out_g = 0;
static uint16_t out_b = 0;
static bool out_enabled = false;

//crossfade between what was lit before a colour <-> white mode switch and the new mode's output
static uint16_t out_shown[LIGHT_CH_COUNT]; //last values actually pushed (after any blend)
static uint16_t xf_from[LIGHT_CH_COUNT];
static uint32_t xf_total = 0;
static uint32_t xf_remaining = 0;

static uint16_t to_10bit(uint16_t v) {
    if(v == 0) return 0;
    uint32_t o = ((uint32_t)v * SM2235_MAX_VALUE + (PWM_MAX / 2)) / PWM_MAX;
    return o ? (uint16_t)o : 1; /* keep the lowest levels lit instead of rounding to off */
}

/* The SM2235 bus has no acknowledgement, so a lost or corrupted frame (or one swallowed while the chip
 * wakes from standby) would otherwise leave the LEDs wrong until the values next change. After the
 * output stops changing, re-send the final state once; while lit, keep re-sending it periodically,
 * which also recovers from an LED driver reset. */
#define OUTPUT_SETTLE_RESEND_MS 50
#define OUTPUT_REFRESH_MS       2000

static ev_timer_event_t* output_refresh_evt = NULL;

static int light_output_refresh_cb(void* arg) {
    (void)arg;
    if(sm2235_refresh()) {
        return OUTPUT_REFRESH_MS; //lit: keep refreshing at the slower rate
    }
    output_refresh_evt = NULL; //off: clear + standby re-sent once, nothing more to do
    return -1;
}

static void light_output_push(void) {
    HwConfig* hw_cfg = hw_config_get();
    uint16_t out[SM2235_CHANNELS] = {0};
    uint16_t ch[LIGHT_CH_COUNT] = {out_r, out_g, out_b, out_cold, out_warm};

    if(xf_remaining && xf_total) {
        uint32_t alpha = ((xf_total - xf_remaining) * 1024) / xf_total; //0..1024 towards the new mode
        for(uint8_t i = 0; i < LIGHT_CH_COUNT; i++) {
            ch[i] = (uint16_t)(((uint32_t)xf_from[i] * (1024 - alpha) + (uint32_t)ch[i] * alpha) >> 10);
        }
    }
    memcpy(out_shown, ch, sizeof(out_shown));

    if(out_enabled) {
        for(uint8_t i = 0; i < LIGHT_CH_COUNT; i++) {
            out[hw_cfg->out_map[i]] = to_10bit(ch[i]);
        }
    }
    if(sm2235_set(out)) {
        //restart the settle timer on every change; during fades it keeps being pushed back
        if(output_refresh_evt) {
            TL_ZB_TIMER_CANCEL(&output_refresh_evt);
        }
        output_refresh_evt = TL_ZB_TIMER_SCHEDULE(light_output_refresh_cb, NULL, OUTPUT_SETTLE_RESEND_MS);
    }
}

//render a colour (16-bit RGB proportions) at the given 8-bit level onto the RGB LEDs; whites off
static void hw_light_color_update_rgb(uint16_t r, uint16_t g, uint16_t b, uint8_t level) {
    uint32_t level_corrected = (level == 0) ? 0 : gamma_lut[level - 1];

    uint32_t rr = ((uint32_t)r * RGB_GAIN_R) / 100;
    uint32_t gg = ((uint32_t)g * RGB_GAIN_G) / 100;
    uint32_t bb = ((uint32_t)b * RGB_GAIN_B) / 100;

    uint32_t sum = rr + gg + bb;
    uint32_t cap = (65535UL * RGB_SUM_CAP_PCT) / 100;
    if(sum > cap) {
        rr = (rr * cap) / sum;
        gg = (gg * cap) / sum;
        bb = (bb * cap) / sum;
    }

    out_r = (uint16_t)((rr * level_corrected) / 65535);
    out_g = (uint16_t)((gg * level_corrected) / 65535);
    out_b = (uint16_t)((bb * level_corrected) / 65535);
    out_cold = 0;
    out_warm = 0;
    light_output_push();
}

void light_init(void) {
    HwConfig* hw_cfg = hw_config_get();

    uint8_t cur_rgb = hw_cfg->cur_rgb_code > HW_CUR_CODE_LIMIT_RGB ? HW_CUR_CODE_LIMIT_RGB : hw_cfg->cur_rgb_code;
    uint8_t cur_cw = hw_cfg->cur_cw_code > HW_CUR_CODE_LIMIT_CW ? HW_CUR_CODE_LIMIT_CW : hw_cfg->cur_cw_code;

    out_cold = out_warm = 0;
    out_enabled = false;
    sm2235_init(hw_cfg->scl_pin, hw_cfg->sda_pin, cur_rgb, cur_cw); /* leaves the chip in standby */
}

static void hw_light_color_update_temperature(uint16_t color_temperature_mireds, uint8_t level) {
    uint16_t level_corrected = (level == 0) ? 0 : gamma_lut[level - 1];

    ZclLightColorCtrlAttr* p_color = ZCL_COLOR_ATTR_GET();

    uint16_t span = p_color->color_temp_physical_max_mireds - p_color->color_temp_physical_min_mireds;
    uint16_t mireds = CLAMP(color_temperature_mireds, p_color->color_temp_physical_min_mireds,
                            p_color->color_temp_physical_max_mireds);
    uint16_t warm = span ? (uint16_t)(((uint32_t)(mireds - p_color->color_temp_physical_min_mireds) * level_corrected) / span) : 0;
    uint16_t cold = level_corrected - warm;

    out_cold = cold;
    out_warm = warm;
    out_r = out_g = out_b = 0;
    light_output_push();
}

static void light_color_update(void) {
    ZclLightColorCtrlAttr* p_color = ZCL_COLOR_ATTR_GET();
    uint8_t level = (uint8_t)(eff_level_256 >> 8);

    switch(p_color->color_mode) {
    case ZCL_COLOR_MODE_CURRENT_HUE_SATURATION:
        color_hs_to_linear(p_color->enhanced_current_hue, p_color->current_saturation,
                           &rgb_shown[0], &rgb_shown[1], &rgb_shown[2]);
        hw_light_color_update_rgb(rgb_shown[0], rgb_shown[1], rgb_shown[2], level);
        break;
    case ZCL_COLOR_MODE_CURRENT_X_Y:
        if(blend_total && color_remaining_ticks && color_remaining_ticks <= blend_total) {
            uint16_t alpha = (uint16_t)(((blend_total - color_remaining_ticks) * 1024) / blend_total);
            color_blend(blend_from, blend_to, alpha, rgb_shown);
        } else {
            blend_total = 0; //fade finished (or none): x/y attributes now hold the target
            color_xy_to_rgb(p_color->current_x, p_color->current_y, &rgb_shown[0], &rgb_shown[1], &rgb_shown[2]);
        }
        hw_light_color_update_rgb(rgb_shown[0], rgb_shown[1], rgb_shown[2], level);
        break;
    default:
        hw_light_color_update_temperature(p_color->color_temperature_mireds, level);
        break;
    }
}

static void hw_light_on_off_update(bool on_off) {
    out_enabled = on_off;
    sm2235_invalidate(); /* every on/off command transmits, even if the driver thinks nothing changed */
    light_output_push(); /* off: all channels zero -> SM2235 standby */
    if(on_off) {
        status_led_on();
    } else {
        status_led_off();
    }
}

//push the current eff_level_256 to hardware via whichever path (color temp or plain level) is active
static void light_push_eff_level(void) {
    light_color_update();
    light_ctx.light_attrs_changed = true; //ramps bypass light_refresh(), so mark NV-dirty here instead
}

static void light_couple_color_temp(void) {
    ZclLevelAttr* p_level = ZCL_LEVEL_ATTR_GET();
    ZclLightColorCtrlAttr* p_color = ZCL_COLOR_ATTR_GET();

    if(p_level->options & ZCL_LEVEL_OPTIONS_COUPLE_CT_TO_LEVEL) {
        p_color->color_temperature_mireds =
            p_color->color_temp_physical_max_mireds -
            (((p_level->cur_level - p_level->min_level) *
              (p_color->color_temp_physical_max_mireds - p_color->couple_color_temp_to_level_min_mireds)) /
             (p_level->max_level - p_level->min_level));
    }
}

//advance the level ramp by one tick and derive the ZCL remainingTime (1/10s) attribute from the fine-grained tick count
static void light_level_apply_tick(void) {
    ZclLevelAttr* p_level = ZCL_LEVEL_ATTR_GET();

    if((level_step_256 > 0) && ((((int32_t)eff_level_256 + level_step_256) / 256) > ZCL_LEVEL_ATTR_MAX_LEVEL)) {
        eff_level_256 = (uint16_t)ZCL_LEVEL_ATTR_MAX_LEVEL * 256;
    } else if((level_step_256 < 0) && ((((int32_t)eff_level_256 + level_step_256) / 256) < ZCL_LEVEL_ATTR_MIN_LEVEL)) {
        eff_level_256 = (uint16_t)ZCL_LEVEL_ATTR_MIN_LEVEL * 256;
    } else {
        eff_level_256 += level_step_256;
    }

    p_level->cur_level = (level_step_256 > 0) ? ((eff_level_256 + 127) / 256) : (eff_level_256 / 256);

    if(level_remaining_ticks == 0) {
        eff_level_256 = (uint16_t)p_level->cur_level * 256;
        level_step_256 = 0;
    } else if(level_remaining_ticks != TRANSITION_TICKS_INFINITE) {
        level_remaining_ticks--;
    }

    p_level->remaining_time =
        (level_remaining_ticks == TRANSITION_TICKS_INFINITE) ?
            0xFFFF :
            (uint16_t)((level_remaining_ticks + ZCL_TRANSITION_TICKS_PER_UNIT - 1) / ZCL_TRANSITION_TICKS_PER_UNIT);
}

//advance the color temperature ramp by one tick; unlike level, ZCL defines no remainingTime-style attribute to derive
static void light_color_temp_apply_tick(void) {
    ZclLightColorCtrlAttr* p_color = ZCL_COLOR_ATTR_GET();

    if((color_temp_step_256 > 0) &&
       ((((int32_t)color_temp_current_256 + color_temp_step_256) / 256) > color_temp_max_mireds)) {
        color_temp_current_256 = (uint32_t)color_temp_max_mireds * 256;
    } else if(
        (color_temp_step_256 < 0) &&
        ((((int32_t)color_temp_current_256 + color_temp_step_256) / 256) < color_temp_min_mireds)) {
        color_temp_current_256 = (uint32_t)color_temp_min_mireds * 256;
    } else {
        color_temp_current_256 += color_temp_step_256;
    }

    p_color->color_temperature_mireds = (color_temp_step_256 > 0) ? ((color_temp_current_256 + 127) / 256) :
                                                                    (color_temp_current_256 / 256);

    if(color_temp_remaining_ticks == 0) {
        color_temp_current_256 = (uint32_t)p_color->color_temperature_mireds * 256;
        color_temp_step_256 = 0;
    } else if(color_temp_remaining_ticks != TRANSITION_TICKS_INFINITE) {
        color_temp_remaining_ticks--;
    }
}

static int32_t clamp_q8(int32_t v, int32_t max_units, bool* hit) {
    int32_t max_q8 = max_units << 8;
    if(v < 0) {
        *hit = true;
        return 0;
    }
    if(v > max_q8) {
        *hit = true;
        return max_q8;
    }
    return v;
}

//publish the fixed-point colour state into the ZCL attributes
static void light_color_attrs_sync(void) {
    ZclLightColorCtrlAttr* p_color = ZCL_COLOR_ATTR_GET();

    p_color->enhanced_current_hue = (uint16_t)(hue_q8 >> 8);
    uint8_t hue8 = (uint8_t)(p_color->enhanced_current_hue >> 8);
    p_color->current_hue = hue8 > SAT_MAX ? SAT_MAX : hue8;
    p_color->current_saturation = (uint8_t)(sat_q8 >> 8);
    p_color->current_x = (uint16_t)(x_q8 >> 8);
    p_color->current_y = (uint16_t)(y_q8 >> 8);
}

//advance the hue/saturation/XY ramp by one tick
static void light_color_apply_tick(void) {
    bool hit;

    if(color_ramp_which & LIGHT_RAMP_HUE) {
        hue_q8 = (hue_q8 + hue_step_q8) % HUE_Q8_WRAP;
        if(hue_q8 < 0) hue_q8 += HUE_Q8_WRAP;
    }
    if(color_ramp_which & LIGHT_RAMP_SAT) {
        hit = false;
        sat_q8 = clamp_q8(sat_q8 + sat_step_q8, SAT_MAX, &hit);
        if(hit && color_remaining_ticks == TRANSITION_TICKS_INFINITE) color_ramp_which &= ~LIGHT_RAMP_SAT;
    }
    if(color_ramp_which & LIGHT_RAMP_X) {
        hit = false;
        x_q8 = clamp_q8(x_q8 + x_step_q8, XY_MAX, &hit);
        if(hit && color_remaining_ticks == TRANSITION_TICKS_INFINITE) color_ramp_which &= ~LIGHT_RAMP_X;
    }
    if(color_ramp_which & LIGHT_RAMP_Y) {
        hit = false;
        y_q8 = clamp_q8(y_q8 + y_step_q8, XY_MAX, &hit);
        if(hit && color_remaining_ticks == TRANSITION_TICKS_INFINITE) color_ramp_which &= ~LIGHT_RAMP_Y;
    }

    if(color_remaining_ticks == TRANSITION_TICKS_INFINITE) {
        if(color_ramp_which == 0) {
            color_remaining_ticks = 0; //every moving component reached its limit
        }
    } else if(color_remaining_ticks) {
        color_remaining_ticks--;
        if(color_remaining_ticks == 0) {
            //land exactly on the requested targets
            if(color_ramp_which & LIGHT_RAMP_HUE) hue_q8 = (int32_t)color_target_hue << 8;
            if(color_ramp_which & LIGHT_RAMP_SAT) sat_q8 = (int32_t)color_target_sat << 8;
            if(color_ramp_which & LIGHT_RAMP_X) x_q8 = (int32_t)color_target_x << 8;
            if(color_ramp_which & LIGHT_RAMP_Y) y_q8 = (int32_t)color_target_y << 8;
            color_ramp_which = 0;
        }
    }

    light_color_attrs_sync();
}

//start/stop the shared timer depending on whether any ramp is still active
static int light_transition_timer_cb(void* arg);
static void light_transition_timer_sync(void) {
    bool needed = (transition_mode != TRANSITION_NONE) || (color_temp_remaining_ticks != 0) ||
                  (color_remaining_ticks != 0) || (xf_remaining != 0);

    if(needed) {
        if(!transition_timer_evt) {
            transition_timer_evt = TL_ZB_TIMER_SCHEDULE(light_transition_timer_cb, NULL, ZCL_TRANSITION_INTERVAL);
        }
    } else if(transition_timer_evt) {
        TL_ZB_TIMER_CANCEL(&transition_timer_evt);
    }
}

//single shared timer tick, advances whichever ramp(s) are currently active
static int light_transition_timer_cb(void* arg) {
    if(transition_mode == TRANSITION_ONOFF) {
        if(on_off_fade_remaining) {
            on_off_fade_remaining--;
            eff_level_256 = on_off_fade_remaining ? (uint16_t)((int32_t)eff_level_256 + on_off_fade_step_256) :
                                                    on_off_fade_target_256;
        }

        if(on_off_fade_remaining == 0) {
            if(on_off_fade_target_256 == 0) {
                hw_light_on_off_update(false); //fully dimmed, now safe to disable PWM
            }
            transition_mode = TRANSITION_NONE;
        }
    } else if(transition_mode == TRANSITION_LEVEL) {
        ZclLevelAttr* p_level = ZCL_LEVEL_ATTR_GET();

        if(level_remaining_ticks) {
            if((level_remaining_ticks != TRANSITION_TICKS_INFINITE) && (level_compensation >= level_remaining_ticks)) {
                level_compensation = 0;
                level_step_256 += (level_step_256 > 0) ? 1 : -1;
            }

            light_level_apply_tick();
            light_couple_color_temp();

            if(level_remaining_ticks == TRANSITION_TICKS_INFINITE) {
                if(((level_step_256 > 0) && (p_level->cur_level >= ZCL_LEVEL_ATTR_MAX_LEVEL)) ||
                   ((level_step_256 < 0) && (p_level->cur_level <= ZCL_LEVEL_ATTR_MIN_LEVEL))) {
                    level_remaining_ticks = 0;
                    p_level->remaining_time = 0;
                }
            }
        }

        if(level_with_on_off && (p_level->cur_level <= ZCL_LEVEL_ATTR_MIN_LEVEL)) {
            light_on_off_update(ZCL_CMD_ONOFF_OFF);
        }

        if(level_remaining_ticks == 0) {
            transition_mode = TRANSITION_NONE;
        }
    }

    if(color_temp_remaining_ticks) {
        light_color_temp_apply_tick();

        if(color_temp_remaining_ticks == TRANSITION_TICKS_INFINITE) {
            ZclLightColorCtrlAttr* p_color = ZCL_COLOR_ATTR_GET();
            if(((color_temp_step_256 > 0) && (p_color->color_temperature_mireds >= color_temp_max_mireds)) ||
               ((color_temp_step_256 < 0) && (p_color->color_temperature_mireds <= color_temp_min_mireds))) {
                color_temp_remaining_ticks = 0;
            }
        }
    }

    if(color_remaining_ticks) {
        light_color_apply_tick();
    }

    if(xf_remaining) {
        xf_remaining--;
    }

    light_push_eff_level();

    if((transition_mode != TRANSITION_NONE) || (color_temp_remaining_ticks != 0) || (color_remaining_ticks != 0) ||
       (xf_remaining != 0)) {
        return 0;
    }

    transition_timer_evt = NULL;
    return -1;
}

//ramp brightness between 0 and curLevel over onOffTransitionTime when the OnOff cluster turns the light on/off
static void light_on_off_transition_start(bool turn_on) {
    ZclLevelAttr* p_level = ZCL_LEVEL_ATTR_GET();

    if(transition_mode == TRANSITION_LEVEL) {
        //a level cluster command is actively ramping brightness; just gate the PWM hw, don't disturb its timer
        hw_light_on_off_update(turn_on);
        return;
    }

    uint32_t ticks = (uint32_t)p_level->on_off_transition_time * ZCL_TRANSITION_TICKS_PER_UNIT;

    transition_mode = TRANSITION_ONOFF;
    on_off_fade_target_256 = turn_on ? ((uint16_t)p_level->cur_level << 8) : 0;

    if(turn_on) {
        hw_light_on_off_update(true); //enable PWM hw now, duty still ramps up from its current (off) value
    }

    if(ticks == 0) {
        eff_level_256 = on_off_fade_target_256;
        light_push_eff_level();
        if(!turn_on) {
            hw_light_on_off_update(false);
        }
        transition_mode = TRANSITION_NONE;
        light_transition_timer_sync();
        return;
    }

    on_off_fade_step_256 = ((int32_t)on_off_fade_target_256 - (int32_t)eff_level_256) / (int32_t)ticks;
    on_off_fade_remaining = ticks;
    light_transition_timer_sync();
}

//ramp curLevel toward targetLevel over transitionTimeZcl (ZCL 1/10s units); 0xFFFF -> use onOffTransitionTime, or as fast as possible
void light_level_ramp_to_level(uint8_t target_level, uint16_t transition_time_zcl, bool with_on_off) {
    ZclLevelAttr* p_level = ZCL_LEVEL_ATTR_GET();

    uint16_t zcl_time;
    if(transition_time_zcl == 0xFFFF) {
        zcl_time = p_level->on_off_transition_time ? p_level->on_off_transition_time : 1;
    } else if(transition_time_zcl == 0) {
        zcl_time = 1;
    } else {
        zcl_time = transition_time_zcl;
    }

    transition_mode = TRANSITION_LEVEL;
    level_with_on_off = with_on_off;
    eff_level_256 = (uint16_t)p_level->cur_level << 8;
    level_remaining_ticks = (uint32_t)zcl_time * ZCL_TRANSITION_TICKS_PER_UNIT;

    int32_t step_256 = ((int32_t)(target_level - p_level->cur_level)) << 8;
    level_step_256 = step_256 / (int32_t)level_remaining_ticks;

    step_256 = (step_256 > 0) ? step_256 : -step_256;
    level_compensation = (uint32_t)(step_256 % (int32_t)level_remaining_ticks);

    light_level_apply_tick();
    light_couple_color_temp();
    light_push_eff_level();

    if(with_on_off) {
        if(level_step_256 > 0) {
            light_on_off_update(ZCL_CMD_ONOFF_ON);
        } else if(p_level->cur_level <= ZCL_LEVEL_ATTR_MIN_LEVEL) {
            light_on_off_update(ZCL_CMD_ONOFF_OFF);
        }
    }

    if(level_remaining_ticks == 0) {
        transition_mode = TRANSITION_NONE;
    }

    light_transition_timer_sync();
}

//continuous rate-based ramp (ZCL Move command); runs until Stop or min/max level reached
void light_level_ramp_at_rate(uint8_t rate, bool move_up, bool with_on_off) {
    ZclLevelAttr* p_level = ZCL_LEVEL_ATTR_GET();

    transition_mode = TRANSITION_LEVEL;
    level_with_on_off = with_on_off;
    eff_level_256 = (uint16_t)p_level->cur_level << 8;
    level_step_256 = (((int32_t)rate) << 8) / ZCL_TRANSITION_TICKS_PER_SEC;

    if(move_up) {
        if(with_on_off) {
            light_on_off_update(ZCL_CMD_ONOFF_ON);
        }
    } else {
        level_step_256 = -level_step_256;
    }

    level_remaining_ticks = TRANSITION_TICKS_INFINITE;
    level_compensation = 0;

    light_level_apply_tick();
    light_couple_color_temp();
    light_push_eff_level();

    if(with_on_off && (p_level->cur_level <= ZCL_LEVEL_ATTR_MIN_LEVEL)) {
        light_on_off_update(ZCL_CMD_ONOFF_OFF);
    }

    light_transition_timer_sync();
}

//cancel an in-progress level ramp (ZCL Stop command)
void light_level_ramp_stop(void) {
    ZclLevelAttr* p_level = ZCL_LEVEL_ATTR_GET();
    p_level->remaining_time = 0;
    level_remaining_ticks = 0;

    if(transition_mode == TRANSITION_LEVEL) {
        transition_mode = TRANSITION_NONE;
    }

    light_transition_timer_sync();
}

//ramp colorTemperatureMireds toward targetMireds over transitionTimeZcl (ZCL 1/10s units)
void light_color_temp_ramp_to(
    uint16_t target_mireds,
    uint16_t transition_time_zcl,
    uint16_t min_mireds,
    uint16_t max_mireds) {
    ZclLightColorCtrlAttr* p_color = ZCL_COLOR_ATTR_GET();

    color_temp_min_mireds = min_mireds;
    color_temp_max_mireds = max_mireds;

    uint16_t zcl_time = (transition_time_zcl == 0) ? 1 : transition_time_zcl;

    color_temp_current_256 = (uint32_t)p_color->color_temperature_mireds << 8;
    color_temp_remaining_ticks = (uint32_t)zcl_time * ZCL_TRANSITION_TICKS_PER_UNIT;
    color_temp_step_256 =
        (((int32_t)(target_mireds - p_color->color_temperature_mireds)) << 8) / (int32_t)color_temp_remaining_ticks;

    light_color_temp_apply_tick();
    light_push_eff_level();

    light_transition_timer_sync();
}

//continuous rate-based color temperature ramp (ZCL Move command); runs until Stop or min/max reached
void light_color_temp_ramp_at_rate(uint16_t rate, bool move_up, uint16_t min_mireds, uint16_t max_mireds) {
    color_temp_min_mireds = min_mireds;
    color_temp_max_mireds = max_mireds;

    color_temp_step_256 = (((int32_t)rate) << 8) / ZCL_TRANSITION_TICKS_PER_SEC;
    if(!move_up) {
        color_temp_step_256 = -color_temp_step_256;
    }

    color_temp_remaining_ticks = TRANSITION_TICKS_INFINITE;

    light_color_temp_apply_tick();
    light_push_eff_level();

    light_transition_timer_sync();
}

//cancel an in-progress color temperature ramp (ZCL Stop command)
void light_color_temp_ramp_stop(void) {
    color_temp_remaining_ticks = 0;
    light_transition_timer_sync();
}

//load the fixed-point colour state from the current ZCL attributes before starting a ramp
static void light_color_state_load(void) {
    ZclLightColorCtrlAttr* p_color = ZCL_COLOR_ATTR_GET();

    hue_q8 = (int32_t)p_color->enhanced_current_hue << 8;
    sat_q8 = (int32_t)p_color->current_saturation << 8;
    x_q8 = (int32_t)p_color->current_x << 8;
    y_q8 = (int32_t)p_color->current_y << 8;
}

//end any perceptual XY fade where it stands: move the x/y attributes to the colour actually shown
static void light_color_blend_settle(void) {
    ZclLightColorCtrlAttr* p_color = ZCL_COLOR_ATTR_GET();

    if(blend_total && p_color->color_mode == ZCL_COLOR_MODE_CURRENT_X_Y &&
       (rgb_shown[0] | rgb_shown[1] | rgb_shown[2])) {
        color_rgb_to_xy(rgb_shown[0], rgb_shown[1], rgb_shown[2], &p_color->current_x, &p_color->current_y);
    }
    blend_total = 0;
}

void light_color_shown_rgb(uint16_t* r, uint16_t* g, uint16_t* b) {
    ZclLightColorCtrlAttr* p_color = ZCL_COLOR_ATTR_GET();

    if(rgb_shown[0] | rgb_shown[1] | rgb_shown[2]) {
        *r = rgb_shown[0];
        *g = rgb_shown[1];
        *b = rgb_shown[2];
    } else if(p_color->color_mode == ZCL_COLOR_MODE_CURRENT_X_Y) {
        color_xy_to_rgb(p_color->current_x, p_color->current_y, r, g, b);
    } else {
        color_hs_to_linear(p_color->enhanced_current_hue, p_color->current_saturation, r, g, b);
    }
}

void light_color_ramp_to(
    uint8_t which,
    int32_t hue_delta,
    uint8_t target_sat,
    uint16_t target_x,
    uint16_t target_y,
    uint16_t transition_time_zcl) {
    light_color_blend_settle();
    light_color_state_load();

    //ZCL transition time 0 means "as fast as possible": land on the next tick
    uint32_t ticks = transition_time_zcl ? (uint32_t)transition_time_zcl * ZCL_TRANSITION_TICKS_PER_UNIT : 1;

    if(target_sat > SAT_MAX) target_sat = SAT_MAX;
    if(target_x > XY_MAX) target_x = XY_MAX;
    if(target_y > XY_MAX) target_y = XY_MAX;

    //timed XY fades render as a perceptual blend from what's lit to the target; x/y attributes still ramp for reporting
    if((which & (LIGHT_RAMP_X | LIGHT_RAMP_Y)) && ticks > 1 && (rgb_shown[0] | rgb_shown[1] | rgb_shown[2])) {
        uint16_t tx = (which & LIGHT_RAMP_X) ? target_x : (uint16_t)(x_q8 >> 8);
        uint16_t ty = (which & LIGHT_RAMP_Y) ? target_y : (uint16_t)(y_q8 >> 8);
        memcpy(blend_from, rgb_shown, sizeof(blend_from));
        color_xy_to_rgb(tx, ty, &blend_to[0], &blend_to[1], &blend_to[2]);
        blend_total = ticks;
    }

    color_target_hue = (uint16_t)(((int32_t)(hue_q8 >> 8) + hue_delta) & 0xFFFF);
    color_target_sat = target_sat;
    color_target_x = target_x;
    color_target_y = target_y;

    hue_step_q8 = (which & LIGHT_RAMP_HUE) ? ((hue_delta << 8) / (int32_t)ticks) : 0;
    sat_step_q8 = (which & LIGHT_RAMP_SAT) ? ((((int32_t)target_sat << 8) - sat_q8) / (int32_t)ticks) : 0;
    x_step_q8 = (which & LIGHT_RAMP_X) ? ((((int32_t)target_x << 8) - x_q8) / (int32_t)ticks) : 0;
    y_step_q8 = (which & LIGHT_RAMP_Y) ? ((((int32_t)target_y << 8) - y_q8) / (int32_t)ticks) : 0;

    color_ramp_which = which;
    color_remaining_ticks = ticks;

    light_color_apply_tick();
    light_push_eff_level();
    light_transition_timer_sync();
}

void light_color_ramp_at_rate(uint8_t which, int32_t hue_rate, int32_t sat_rate, int32_t x_rate, int32_t y_rate) {
    light_color_blend_settle();
    light_color_state_load();

    hue_step_q8 = (which & LIGHT_RAMP_HUE) ? ((hue_rate << 8) / ZCL_TRANSITION_TICKS_PER_SEC) : 0;
    sat_step_q8 = (which & LIGHT_RAMP_SAT) ? ((sat_rate << 8) / ZCL_TRANSITION_TICKS_PER_SEC) : 0;
    x_step_q8 = (which & LIGHT_RAMP_X) ? ((x_rate << 8) / ZCL_TRANSITION_TICKS_PER_SEC) : 0;
    y_step_q8 = (which & LIGHT_RAMP_Y) ? ((y_rate << 8) / ZCL_TRANSITION_TICKS_PER_SEC) : 0;

    color_ramp_which = which;
    color_remaining_ticks = which ? TRANSITION_TICKS_INFINITE : 0;

    if(which) {
        light_color_apply_tick();
        light_push_eff_level();
    }
    light_transition_timer_sync();
}

void light_color_ramp_stop(void) {
    light_color_blend_settle();
    color_ramp_which = 0;
    color_remaining_ticks = 0;
    light_transition_timer_sync();
}

void light_mode_crossfade_start(uint16_t transition_time_zcl) {
    if(transition_time_zcl == 0) {
        xf_remaining = 0; //instant switch requested
        return;
    }
    memcpy(xf_from, out_shown, sizeof(xf_from)); //start from whatever is lit right now (even mid-fade)
    xf_total = (uint32_t)transition_time_zcl * ZCL_TRANSITION_TICKS_PER_UNIT;
    xf_remaining = xf_total;
    light_transition_timer_sync();
}

void light_refresh(LightSta sta) {
    switch(sta) {
    case LIGHT_STA_ON_OFF: {
        ZclOnOffAttr* p_on_off = ZCL_ONOFF_ATTR_GET();
        light_on_off_transition_start(p_on_off->on_off);
        break;
    }
    case LIGHT_STA_LEVEL:
    case LIGHT_STA_COLOR:
        //eff_level_256 already reflects the currently displayed brightness (0 until the first on/off fade-in runs)
        light_color_update();
        break;
    default:
        return;
        break;
    }

    light_ctx.light_attrs_changed = true;
}

static void light_blink_hw_set(bool on_off, bool restore) {
    ZclLevelAttr* p_level = ZCL_LEVEL_ATTR_GET();

    uint16_t on_brightness = restore ? ((uint16_t)p_level->cur_level << 8) : (ZCL_LEVEL_ATTR_MAX_LEVEL << 8);
    eff_level_256 = on_off ? on_brightness : 0;
    light_color_update();
    hw_light_on_off_update(on_off);
}

static int light_blink_timer_evt_cb(void* arg) {
    uint32_t interval = 0;

    light_ctx.sta = !light_ctx.sta;
    if(light_ctx.sta) {
        light_blink_hw_set(true, false);
        interval = light_ctx.led_on_time;
    } else {
        light_blink_hw_set(false, false);
        interval = light_ctx.led_off_time;
    }

    if(light_ctx.sta == light_ctx.ori_sta) {
        if(light_ctx.times) {
            light_ctx.times--;
            if(light_ctx.times <= 0) {
                light_blink_hw_set(light_ctx.ori_sta != 0, true);

                light_ctx.timer_led_evt = NULL;
                return -1;
            }
        }
    }

    return interval;
}

void light_blink_start(uint8_t times, uint16_t led_on_time, uint16_t led_off_time) {
    uint32_t interval = 0;
    ZclOnOffAttr* p_on_off = ZCL_ONOFF_ATTR_GET();

    if(!light_ctx.timer_led_evt) {
        light_ctx.times = times;
        light_ctx.led_on_time = led_on_time;
        light_ctx.led_off_time = led_off_time;

        light_ctx.ori_sta = p_on_off->on_off;

        light_ctx.sta = light_ctx.ori_sta;
        interval = light_ctx.sta ? led_on_time : led_off_time;

        light_ctx.timer_led_evt = TL_ZB_TIMER_SCHEDULE(light_blink_timer_evt_cb, NULL, interval);
    }
}

void light_blink_stop(void) {
    if(light_ctx.timer_led_evt) {
        TL_ZB_TIMER_CANCEL(&light_ctx.timer_led_evt);

        light_ctx.times = 0;
        light_blink_hw_set(light_ctx.ori_sta != 0, true);
    }
}

void light_load_state(void) {
    //init attributes/hardware state first so the on/off fade started last has a final target to ramp to
    light_color_init();
    light_level_init();
    light_on_off_init();
}
