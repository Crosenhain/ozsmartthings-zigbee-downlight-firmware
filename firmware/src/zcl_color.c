/*
 * Modified from nminaylov/zigbee-light-cct (Apache-2.0), 2026: hue/saturation, enhanced hue, XY and
 * colour loop commands, HS <-> XY mode conversion. See NOTICE.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tl_common.h"
#include "zb_api.h"
#include "zcl_include.h"
#include "app.h"
#include "light_control.h"
#include "color_math.h"

#define SAT_MAX 254
#define XY_MAX  0xFEFF

void light_color_init(void) {
    ZclLightColorCtrlAttr* p_color = ZCL_COLOR_ATTR_GET();

    if(p_color->color_temperature_mireds < p_color->color_temp_physical_min_mireds) {
        p_color->color_temperature_mireds = p_color->color_temp_physical_min_mireds;
    } else if(p_color->color_temperature_mireds > p_color->color_temp_physical_max_mireds) {
        p_color->color_temperature_mireds = p_color->color_temp_physical_max_mireds;
    }
    if(p_color->current_saturation > SAT_MAX) p_color->current_saturation = SAT_MAX;
    if(p_color->current_x > XY_MAX) p_color->current_x = XY_MAX;
    if(p_color->current_y > XY_MAX) p_color->current_y = XY_MAX;

    uint8_t hue8 = (uint8_t)(p_color->enhanced_current_hue >> 8);
    p_color->current_hue = hue8 > SAT_MAX ? SAT_MAX : hue8;

    light_refresh(LIGHT_STA_COLOR);
}

/* Switch colour mode and return the transition time the caller's ramp should use.
 * - colour <-> colour temperature: crossfade the lit output over ttime and let the new
 *   mode's ramp jump straight to its target (returns 0);
 * - HS <-> XY: convert the colour currently lit (even mid-fade) into the new mode's coordinates
 *   so the ramp starts from what's on the LEDs, not from stale attributes (returns ttime). */
static uint16_t light_set_color_mode(uint8_t color_mode, uint8_t enhanced_color_mode, uint16_t ttime) {
    ZclLightColorCtrlAttr* p_color = ZCL_COLOR_ATTR_GET();
    uint8_t old_mode = p_color->color_mode;
    uint16_t ramp_time = ttime;

    if(old_mode != color_mode) {
        bool old_ct = (old_mode == ZCL_COLOR_MODE_COLOR_TEMPERATURE_MIREDS);
        bool new_ct = (color_mode == ZCL_COLOR_MODE_COLOR_TEMPERATURE_MIREDS);
        uint16_t r, g, b;

        if(old_ct != new_ct) {
            light_mode_crossfade_start(ttime);
            ramp_time = 0;
        } else if(old_mode == ZCL_COLOR_MODE_CURRENT_HUE_SATURATION && color_mode == ZCL_COLOR_MODE_CURRENT_X_Y) {
            light_color_shown_rgb(&r, &g, &b);
            color_rgb_to_xy(r, g, b, &p_color->current_x, &p_color->current_y);
        } else if(old_mode == ZCL_COLOR_MODE_CURRENT_X_Y && color_mode == ZCL_COLOR_MODE_CURRENT_HUE_SATURATION) {
            uint16_t hue;
            uint8_t sat;
            light_color_shown_rgb(&r, &g, &b);
            color_rgb_to_hs(color_encode(r), color_encode(g), color_encode(b), &hue, &sat);
            p_color->enhanced_current_hue = hue;
            p_color->current_saturation = sat;
            uint8_t hue8 = (uint8_t)(hue >> 8);
            p_color->current_hue = hue8 > SAT_MAX ? SAT_MAX : hue8;
        }
    }

    if(color_mode == ZCL_COLOR_MODE_COLOR_TEMPERATURE_MIREDS) {
        light_color_ramp_stop();
    } else {
        light_color_temp_ramp_stop();
    }
    p_color->color_mode = color_mode;
    p_color->enhanced_color_mode = enhanced_color_mode;
    return ramp_time;
}


static void hs_move_to(uint8_t which, uint16_t enh_hue, uint8_t direction, uint8_t sat, uint16_t ttime, bool enhanced) {
    ZclLightColorCtrlAttr* p_color = ZCL_COLOR_ATTR_GET();

    ttime = light_set_color_mode(ZCL_COLOR_MODE_CURRENT_HUE_SATURATION,
                                 enhanced ? ZCL_ENHANCED_COLOR_MODE_CURRENT_HUE_SATURATION : ZCL_COLOR_MODE_CURRENT_HUE_SATURATION,
                                 ttime);
    int32_t delta = (which & LIGHT_RAMP_HUE) ? color_hue_delta(p_color->enhanced_current_hue, enh_hue, direction) : 0;
    light_color_ramp_to(which, delta, sat, p_color->current_x, p_color->current_y, ttime);
}

static status_t hs_move(uint8_t which, uint8_t move_mode, int32_t rate, bool enhanced) {
    if(move_mode == COLOR_CTRL_MOVE_STOP) {
        light_color_ramp_stop();
        return ZCL_STA_SUCCESS;
    }
    if(rate == 0 || (move_mode != COLOR_CTRL_MOVE_UP && move_mode != COLOR_CTRL_MOVE_DOWN)) {
        return ZCL_STA_INVALID_FIELD;
    }
    light_set_color_mode(ZCL_COLOR_MODE_CURRENT_HUE_SATURATION,
                         enhanced ? ZCL_ENHANCED_COLOR_MODE_CURRENT_HUE_SATURATION : ZCL_COLOR_MODE_CURRENT_HUE_SATURATION, 0);
    int32_t signed_rate = (move_mode == COLOR_CTRL_MOVE_UP) ? rate : -rate;
    if(which & LIGHT_RAMP_HUE) {
        light_color_ramp_at_rate(LIGHT_RAMP_HUE, signed_rate, 0, 0, 0);
    } else {
        light_color_ramp_at_rate(LIGHT_RAMP_SAT, 0, signed_rate, 0, 0);
    }
    return ZCL_STA_SUCCESS;
}

static status_t hs_step(uint8_t which, uint8_t step_mode, int32_t step, uint16_t ttime, bool enhanced) {
    ZclLightColorCtrlAttr* p_color = ZCL_COLOR_ATTR_GET();

    if(step_mode != COLOR_CTRL_STEP_MODE_UP && step_mode != COLOR_CTRL_STEP_MODE_DOWN) {
        return ZCL_STA_INVALID_FIELD;
    }
    ttime = light_set_color_mode(ZCL_COLOR_MODE_CURRENT_HUE_SATURATION,
                                 enhanced ? ZCL_ENHANCED_COLOR_MODE_CURRENT_HUE_SATURATION : ZCL_COLOR_MODE_CURRENT_HUE_SATURATION,
                                 ttime);
    if(step_mode == COLOR_CTRL_STEP_MODE_DOWN) step = -step;

    if(which & LIGHT_RAMP_HUE) {
        light_color_ramp_to(LIGHT_RAMP_HUE, step, 0, 0, 0, ttime);
    } else {
        int32_t sat = (int32_t)p_color->current_saturation + step;
        if(sat < 0) sat = 0;
        if(sat > SAT_MAX) sat = SAT_MAX;
        light_color_ramp_to(LIGHT_RAMP_SAT, 0, (uint8_t)sat, 0, 0, ttime);
    }
    return ZCL_STA_SUCCESS;
}

static void light_resolve_color_temp_limits(
    uint16_t cmd_min_mireds,
    uint16_t cmd_max_mireds,
    uint16_t* min_mireds,
    uint16_t* max_mireds) {
    ZclLightColorCtrlAttr* p_color = ZCL_COLOR_ATTR_GET();

    *min_mireds = cmd_min_mireds ? max2(cmd_min_mireds, p_color->color_temp_physical_min_mireds) :
                                   p_color->color_temp_physical_min_mireds;
    *max_mireds = cmd_max_mireds ? min2(cmd_max_mireds, p_color->color_temp_physical_max_mireds) :
                                   p_color->color_temp_physical_max_mireds;
}

static void light_move_to_color_temperature_process(zcl_colorCtrlMoveToColorTemperatureCmd_t* cmd) {
    ZclLightColorCtrlAttr* p_color = ZCL_COLOR_ATTR_GET();

    uint16_t ttime = light_set_color_mode(ZCL_COLOR_MODE_COLOR_TEMPERATURE_MIREDS, ZCL_COLOR_MODE_COLOR_TEMPERATURE_MIREDS,
                                          cmd->transitionTime);

    uint16_t min_mireds = p_color->color_temp_physical_min_mireds;
    uint16_t max_mireds = p_color->color_temp_physical_max_mireds;

    uint16_t target = cmd->colorTemperature;
    if(target < min_mireds) target = min_mireds;
    if(target > max_mireds) target = max_mireds;

    light_color_temp_ramp_to(target, ttime, min_mireds, max_mireds);
}

static status_t light_move_color_temperature_process(zcl_colorCtrlMoveColorTemperatureCmd_t* cmd) {
    if((cmd->moveMode == COLOR_CTRL_MOVE_UP) || (cmd->moveMode == COLOR_CTRL_MOVE_DOWN)) {
        if(cmd->rate == 0) {
            return ZCL_STA_INVALID_FIELD;
        }
    }

    light_set_color_mode(ZCL_COLOR_MODE_COLOR_TEMPERATURE_MIREDS, ZCL_COLOR_MODE_COLOR_TEMPERATURE_MIREDS, 0);

    uint16_t min_mireds, max_mireds;
    light_resolve_color_temp_limits(cmd->colorTempMinMireds, cmd->colorTempMaxMireds, &min_mireds, &max_mireds);

    if(cmd->moveMode == COLOR_CTRL_MOVE_STOP) {
        light_color_temp_ramp_stop();
    } else {
        light_color_temp_ramp_at_rate(cmd->rate, cmd->moveMode == COLOR_CTRL_MOVE_UP, min_mireds, max_mireds);
    }

    return ZCL_STA_SUCCESS;
}

static void light_step_color_temperature_process(zcl_colorCtrlStepColorTemperatureCmd_t* cmd) {
    ZclLightColorCtrlAttr* p_color = ZCL_COLOR_ATTR_GET();

    uint16_t ttime = light_set_color_mode(ZCL_COLOR_MODE_COLOR_TEMPERATURE_MIREDS, ZCL_COLOR_MODE_COLOR_TEMPERATURE_MIREDS,
                                          cmd->transitionTime);

    uint16_t min_mireds, max_mireds;
    light_resolve_color_temp_limits(cmd->colorTempMinMireds, cmd->colorTempMaxMireds, &min_mireds, &max_mireds);

    int32_t target = (cmd->stepMode == COLOR_CTRL_STEP_MODE_UP) ?
                         ((int32_t)p_color->color_temperature_mireds + cmd->stepSize) :
                         ((int32_t)p_color->color_temperature_mireds - cmd->stepSize);
    if(target < min_mireds) target = min_mireds;
    if(target > max_mireds) target = max_mireds;

    light_color_temp_ramp_to((uint16_t)target, ttime, min_mireds, max_mireds);
}

static void xy_move_to(uint16_t x, uint16_t y, uint16_t ttime) {
    ttime = light_set_color_mode(ZCL_COLOR_MODE_CURRENT_X_Y, ZCL_COLOR_MODE_CURRENT_X_Y, ttime);
    light_color_ramp_to(LIGHT_RAMP_X | LIGHT_RAMP_Y, 0, 0, x > XY_MAX ? XY_MAX : x, y > XY_MAX ? XY_MAX : y, ttime);
}

static status_t light_move_color_process(zcl_colorCtrlMoveColorCmd_t* cmd) {
    if(cmd->rateX == 0 && cmd->rateY == 0) {
        light_color_ramp_stop();
        return ZCL_STA_SUCCESS;
    }
    light_set_color_mode(ZCL_COLOR_MODE_CURRENT_X_Y, ZCL_COLOR_MODE_CURRENT_X_Y, 0);
    uint8_t which =(cmd->rateX ? LIGHT_RAMP_X : 0) | (cmd->rateY ? LIGHT_RAMP_Y : 0);
    light_color_ramp_at_rate(which, 0, 0, cmd->rateX, cmd->rateY);
    return ZCL_STA_SUCCESS;
}

static void light_step_color_process(zcl_colorCtrlStepColorCmd_t* cmd) {
    ZclLightColorCtrlAttr* p_color = ZCL_COLOR_ATTR_GET();

    int32_t x = (int32_t)p_color->current_x + cmd->stepX;
    int32_t y = (int32_t)p_color->current_y + cmd->stepY;
    if(x < 0) x = 0;
    if(y < 0) y = 0;
    if(x > XY_MAX) x = XY_MAX;
    if(y > XY_MAX) y = XY_MAX;
    xy_move_to((uint16_t)x, (uint16_t)y, cmd->transitionTime);
}

static void light_stop_move_step_process(void) {
    ZclLightColorCtrlAttr* p_color = ZCL_COLOR_ATTR_GET();

    light_color_temp_ramp_stop();
    if(!p_color->color_loop_active) {
        light_color_ramp_stop(); //ZCL: Stop Move Step has no effect on an active colour loop
    }
}

/* ---- Colour loop (ColorLoopSet) ----
 * The loop is an endless hue move at 65536 / ColorLoopTime units per second. Any other command that sets
 * colour or colour temperature ends it where it stands. Deactivating restores what was lit before the loop
 * started (the stored hue, or the previous XY colour or colour temperature), fading over 1 s. */
#define COLOR_LOOP_EDGE_FADE_ZCL 10

static uint8_t loop_prev_mode = ZCL_COLOR_MODE_CURRENT_HUE_SATURATION;
static uint16_t loop_prev_x = 0, loop_prev_y = 0;

static void color_loop_run(void) {
    ZclLightColorCtrlAttr* p_color = ZCL_COLOR_ATTR_GET();
    uint16_t seconds = p_color->color_loop_time ? p_color->color_loop_time : 1;
    int32_t rate = (int32_t)(65536UL / seconds);

    if(rate < 1) rate = 1;
    light_color_ramp_at_rate(LIGHT_RAMP_HUE, p_color->color_loop_direction ? rate : -rate, 0, 0, 0);
}

//another colour command arrived: stop looping and let that command take over from the colour on the LEDs
static void color_loop_halt(void) {
    ZclLightColorCtrlAttr* p_color = ZCL_COLOR_ATTR_GET();

    if(p_color->color_loop_active) {
        p_color->color_loop_active = 0;
        light_color_ramp_stop();
    }
}

static status_t light_color_loop_set_process(zcl_colorCtrlColorLoopSetCmd_t* cmd) {
    ZclLightColorCtrlAttr* p_color = ZCL_COLOR_ATTR_GET();
    uint8_t flags = cmd->updateFlags.updateFlags;
    bool update_action = flags & 0x01;
    bool update_direction = flags & 0x02;
    bool update_time = flags & 0x04;
    bool update_start_hue = flags & 0x08;

    if((update_action && cmd->action > COLOR_LOOP_SET_ACTION_FROM_ENHANCED_CURRENT_HUE) ||
       (update_direction && cmd->direction > COLOR_LOOP_SET_INCREMENT)) {
        return ZCL_STA_INVALID_FIELD;
    }

    if(update_direction) p_color->color_loop_direction = cmd->direction;
    if(update_time) p_color->color_loop_time = cmd->time;
    if(update_start_hue) p_color->color_loop_start_enhanced_hue = cmd->startHue;

    if(update_action && cmd->action == COLOR_LOOP_SET_DEACTION) {
        if(p_color->color_loop_active) {
            p_color->color_loop_active = 0;
            if(loop_prev_mode == ZCL_COLOR_MODE_COLOR_TEMPERATURE_MIREDS) {
                zcl_colorCtrlMoveToColorTemperatureCmd_t ct = {0};
                ct.colorTemperature = p_color->color_temperature_mireds;
                ct.transitionTime = COLOR_LOOP_EDGE_FADE_ZCL;
                light_move_to_color_temperature_process(&ct);
            } else if(loop_prev_mode == ZCL_COLOR_MODE_CURRENT_X_Y) {
                xy_move_to(loop_prev_x, loop_prev_y, COLOR_LOOP_EDGE_FADE_ZCL);
            } else {
                hs_move_to(LIGHT_RAMP_HUE, p_color->color_loop_stored_enhanced_hue, COLOR_CTRL_DIRECTION_SHORTEST_DISTANCE,
                           0, COLOR_LOOP_EDGE_FADE_ZCL, true);
            }
        }
        return ZCL_STA_SUCCESS;
    }

    if(update_action) {
        if(!p_color->color_loop_active) {
            loop_prev_mode = p_color->color_mode;
            loop_prev_x = p_color->current_x;
            loop_prev_y = p_color->current_y;
        }
        light_set_color_mode(ZCL_COLOR_MODE_CURRENT_HUE_SATURATION, ZCL_ENHANCED_COLOR_MODE_CURRENT_HUE_SATURATION,
                             COLOR_LOOP_EDGE_FADE_ZCL);
        if(!p_color->color_loop_active) {
            p_color->color_loop_stored_enhanced_hue = p_color->enhanced_current_hue;
            if(loop_prev_mode == ZCL_COLOR_MODE_COLOR_TEMPERATURE_MIREDS) {
                p_color->current_saturation = SAT_MAX; //white has no saturation to loop with: use full colour
            }
        }
        if(cmd->action == COLOR_LOOP_SET_ACTION_FROM_COLOR_LOOP_START_ENHANCED_HUE) {
            p_color->enhanced_current_hue = p_color->color_loop_start_enhanced_hue;
        }
        uint8_t hue8 = (uint8_t)(p_color->enhanced_current_hue >> 8);
        p_color->current_hue = hue8 > SAT_MAX ? SAT_MAX : hue8;
        p_color->color_loop_active = 1;
        color_loop_run();
    } else if(p_color->color_loop_active && (update_direction || update_time)) {
        color_loop_run(); //new speed or direction, continuing from the current hue
    }
    return ZCL_STA_SUCCESS;
}

status_t light_color_ctrl_cb(zclIncomingAddrInfo_t* pAddrInfo, uint8_t cmd_id, void* cmd_payload) {
    status_t status = ZCL_STA_SUCCESS;

    if(pAddrInfo->dstEp != LIGHT_ENDPOINT) {
        return status;
    }

    if(cmd_id != ZCL_CMD_LIGHT_COLOR_CONTROL_COLOR_LOOP_SET && cmd_id != ZCL_CMD_LIGHT_COLOR_CONTROL_STOP_MOVE_STEP) {
        color_loop_halt();
    }

    switch(cmd_id) {
    case ZCL_CMD_LIGHT_COLOR_CONTROL_MOVE_TO_HUE: {
        zcl_colorCtrlMoveToHueCmd_t* cmd = cmd_payload;
        hs_move_to(LIGHT_RAMP_HUE, (uint16_t)cmd->hue << 8, cmd->direction, 0, cmd->transitionTime, false);
        break;
    }
    case ZCL_CMD_LIGHT_COLOR_CONTROL_MOVE_HUE: {
        zcl_colorCtrlMoveHueCmd_t* cmd = cmd_payload;
        status = hs_move(LIGHT_RAMP_HUE, cmd->moveMode, (int32_t)cmd->rate << 8, false);
        break;
    }
    case ZCL_CMD_LIGHT_COLOR_CONTROL_STEP_HUE: {
        zcl_colorCtrlStepHueCmd_t* cmd = cmd_payload;
        status = hs_step(LIGHT_RAMP_HUE, cmd->stepMode, (int32_t)cmd->stepSize << 8, cmd->transitionTime, false);
        break;
    }
    case ZCL_CMD_LIGHT_COLOR_CONTROL_MOVE_TO_SATURATION: {
        zcl_colorCtrlMoveToSaturationCmd_t* cmd = cmd_payload;
        hs_move_to(LIGHT_RAMP_SAT, 0, 0, cmd->saturation, cmd->transitionTime, false);
        break;
    }
    case ZCL_CMD_LIGHT_COLOR_CONTROL_MOVE_SATURATION: {
        zcl_colorCtrlMoveSaturationCmd_t* cmd = cmd_payload;
        status = hs_move(LIGHT_RAMP_SAT, cmd->moveMode, cmd->rate, false);
        break;
    }
    case ZCL_CMD_LIGHT_COLOR_CONTROL_STEP_SATURATION: {
        zcl_colorCtrlStepSaturationCmd_t* cmd = cmd_payload;
        status = hs_step(LIGHT_RAMP_SAT, cmd->stepMode, cmd->stepSize, cmd->transitionTime, false);
        break;
    }
    case ZCL_CMD_LIGHT_COLOR_CONTROL_MOVE_TO_HUE_AND_SATURATION: {
        zcl_colorCtrlMoveToHueAndSaturationCmd_t* cmd = cmd_payload;
        hs_move_to(LIGHT_RAMP_HUE | LIGHT_RAMP_SAT, (uint16_t)cmd->hue << 8, COLOR_CTRL_DIRECTION_SHORTEST_DISTANCE,
                   cmd->saturation, cmd->transitionTime, false);
        break;
    }
    case ZCL_CMD_LIGHT_COLOR_CONTROL_MOVE_TO_COLOR: {
        zcl_colorCtrlMoveToColorCmd_t* cmd = cmd_payload;
        xy_move_to(cmd->colorX, cmd->colorY, cmd->transitionTime);
        break;
    }
    case ZCL_CMD_LIGHT_COLOR_CONTROL_MOVE_COLOR:
        status = light_move_color_process((zcl_colorCtrlMoveColorCmd_t*)cmd_payload);
        break;
    case ZCL_CMD_LIGHT_COLOR_CONTROL_STEP_COLOR:
        light_step_color_process((zcl_colorCtrlStepColorCmd_t*)cmd_payload);
        break;
    case ZCL_CMD_LIGHT_COLOR_CONTROL_ENHANCED_MOVE_TO_HUE: {
        zcl_colorCtrlEnhancedMoveToHueCmd_t* cmd = cmd_payload;
        hs_move_to(LIGHT_RAMP_HUE, cmd->enhancedHue, cmd->direction, 0, cmd->transitionTime, true);
        break;
    }
    case ZCL_CMD_LIGHT_COLOR_CONTROL_ENHANCED_MOVE_HUE: {
        zcl_colorCtrlEnhancedMoveHueCmd_t* cmd = cmd_payload;
        status = hs_move(LIGHT_RAMP_HUE, cmd->moveMode, cmd->rate, true);
        break;
    }
    case ZCL_CMD_LIGHT_COLOR_CONTROL_ENHANCED_STEP_HUE: {
        zcl_colorCtrlEnhancedStepHueCmd_t* cmd = cmd_payload;
        status = hs_step(LIGHT_RAMP_HUE, cmd->stepMode, cmd->stepSize, cmd->transitionTime, true);
        break;
    }
    case ZCL_CMD_LIGHT_COLOR_CONTROL_ENHANCED_MOVE_TO_HUE_AND_SATURATION: {
        zcl_colorCtrlEnhancedMoveToHueAndSaturationCmd_t* cmd = cmd_payload;
        hs_move_to(LIGHT_RAMP_HUE | LIGHT_RAMP_SAT, cmd->enhancedHue, COLOR_CTRL_DIRECTION_SHORTEST_DISTANCE,
                   cmd->saturation, cmd->transitionTime, true);
        break;
    }
    case ZCL_CMD_LIGHT_COLOR_CONTROL_MOVE_TO_COLOR_TEMPERATURE:
        light_move_to_color_temperature_process((zcl_colorCtrlMoveToColorTemperatureCmd_t*)cmd_payload);
        break;
    case ZCL_CMD_LIGHT_COLOR_CONTROL_MOVE_COLOR_TEMPERATURE:
        status = light_move_color_temperature_process((zcl_colorCtrlMoveColorTemperatureCmd_t*)cmd_payload);
        break;
    case ZCL_CMD_LIGHT_COLOR_CONTROL_STEP_COLOR_TEMPERATURE:
        light_step_color_temperature_process((zcl_colorCtrlStepColorTemperatureCmd_t*)cmd_payload);
        break;
    case ZCL_CMD_LIGHT_COLOR_CONTROL_STOP_MOVE_STEP:
        light_stop_move_step_process();
        break;
    case ZCL_CMD_LIGHT_COLOR_CONTROL_COLOR_LOOP_SET:
        status = light_color_loop_set_process((zcl_colorCtrlColorLoopSetCmd_t*)cmd_payload);
        break;
    default:
        status = ZCL_STA_UNSUP_CLUSTER_COMMAND;
        break;
    }

    return status;
}
