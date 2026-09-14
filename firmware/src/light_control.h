/*
 * Modified from nminaylov/zigbee-light-cct (Apache-2.0), 2026: colour ramp, crossfade and shown-colour
 * APIs. See NOTICE.
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once
#include <stdint.h>

typedef enum {
    LIGHT_STA_ON_OFF,
    LIGHT_STA_LEVEL,
    LIGHT_STA_COLOR,
} LightSta;

void light_init(void);
void light_load_state(void);
void light_refresh(LightSta sta);

void light_blink_start(uint8_t times, uint16_t led_on_time, uint16_t led_off_time);
void light_blink_stop(void);

void light_level_ramp_to_level(uint8_t target_level, uint16_t transition_time_zcl, bool with_on_off);
void light_level_ramp_at_rate(uint8_t rate, bool move_up, bool with_on_off);
void light_level_ramp_stop(void);

void light_color_temp_ramp_to(
    uint16_t target_mireds,
    uint16_t transition_time_zcl,
    uint16_t min_mireds,
    uint16_t max_mireds);
void light_color_temp_ramp_at_rate(uint16_t rate, bool move_up, uint16_t min_mireds, uint16_t max_mireds);
void light_color_temp_ramp_stop(void);

/* Hue/saturation and XY ramps (DL41 colour support). Hue is in enhanced units
 * (0..65535, wraps). A ramp moves only the components whose flags are set. */
#define LIGHT_RAMP_HUE (1 << 0)
#define LIGHT_RAMP_SAT (1 << 1)
#define LIGHT_RAMP_X   (1 << 2)
#define LIGHT_RAMP_Y   (1 << 3)

/* Transition to targets over transition_time_zcl (1/10 s). hue_delta is the
 * signed distance to travel (already resolved for direction), in enhanced units. */
void light_color_ramp_to(
    uint8_t which,
    int32_t hue_delta,
    uint8_t target_sat,
    uint16_t target_x,
    uint16_t target_y,
    uint16_t transition_time_zcl);

/* Continuous move at a rate per second until stopped or a bound is reached.
 * hue_rate in enhanced units/s (signed), sat_rate in units/s (signed),
 * x_rate / y_rate in units/s (signed). */
void light_color_ramp_at_rate(uint8_t which, int32_t hue_rate, int32_t sat_rate, int32_t x_rate, int32_t y_rate);

void light_color_ramp_stop(void);

/* Linear RGB of the colour currently lit (brightest channel 65535), including mid-fade.
 * Used to convert between HS and XY without a jump. */
void light_color_shown_rgb(uint16_t* r, uint16_t* g, uint16_t* b);

/* Blend from the currently lit output to the new colour mode's output over the transition time.
 * Call before switching between colour (HS/XY) and colour temperature. 0 = switch instantly. */
void light_mode_crossfade_start(uint16_t transition_time_zcl);
