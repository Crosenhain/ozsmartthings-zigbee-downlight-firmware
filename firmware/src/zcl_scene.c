/*
 * Modified from nminaylov/zigbee-light-cct (Apache-2.0), 2026: colour and colour-loop scene store and
 * recall, black-body xy recalled as colour temperature. See NOTICE.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tl_common.h"
#include "zb_api.h"
#include "zcl_include.h"
#include "app.h"
#include "light_control.h"
#include "color_math.h"

/* Colour Control scene extension field set (ZCL order):
 *   CurrentX(2) CurrentY(2) EnhancedCurrentHue(2) CurrentSaturation(1) ColorLoopActive(1)
 *   ColorLoopDirection(1) ColorLoopTime(2) ColorTemperatureMireds(2) [EnhancedColorMode(1), ZCL7]
 * Controllers may send a shorter set (Zigbee2MQTT's scene_add sends 4 bytes of XY, including for colour
 * temperature), so every field is optional. Stored scenes use the 13-byte form, which fits the 24-byte
 * extension field budget together with OnOff and Level. */
#define COLOR_EXT_LEN               13
#define SCENE_BLACKBODY_MAX_DIST    656 /* 0.01 in xy: close enough to the black-body line to recall as white */

static uint16_t ext_u16(const uint8_t* p) {
    return BUILD_U16(p[0], p[1]);
}

static void light_scene_recall_color(zclIncomingAddrInfo_t* pAddrInfo, const uint8_t* p, uint8_t len, uint16_t ttime) {
    ZclLightColorCtrlAttr* p_color = ZCL_COLOR_ATTR_GET();

    uint16_t x = len >= 4 ? ext_u16(p + 0) : 0;
    uint16_t y = len >= 4 ? ext_u16(p + 2) : 0;
    uint16_t hue = len >= 6 ? ext_u16(p + 4) : 0;
    uint8_t sat = len >= 7 ? p[6] : 0;
    uint8_t loop_active = len >= 8 ? p[7] : 0;
    uint8_t loop_direction = len >= 9 ? p[8] : 0;
    uint16_t loop_time = len >= 11 ? ext_u16(p + 9) : 0;
    uint16_t mireds = len >= 13 ? ext_u16(p + 11) : 0;
    uint8_t mode = len >= 14 ? p[13] : 0xFF;

    if(mode == 0xFF) {
        //no EnhancedColorMode in the scene: infer it from which fields carry values
        if(x || y) {
            mode = ZCL_COLOR_MODE_CURRENT_X_Y;
        } else if(hue || sat || loop_active) {
            mode = ZCL_ENHANCED_COLOR_MODE_CURRENT_HUE_SATURATION;
        } else if(mireds) {
            mode = ZCL_COLOR_MODE_COLOR_TEMPERATURE_MIREDS;
        } else {
            return; //nothing to recall
        }
    }

    if(mode == ZCL_COLOR_MODE_CURRENT_X_Y) {
        //white stored as xy (Z2M does this for colour temperature) belongs on the white LEDs
        uint16_t bb = color_xy_to_blackbody_mireds(x, y, SCENE_BLACKBODY_MAX_DIST);
        if(bb) {
            mode = ZCL_COLOR_MODE_COLOR_TEMPERATURE_MIREDS;
            mireds = bb;
        }
    }

    if(mode == ZCL_COLOR_MODE_COLOR_TEMPERATURE_MIREDS) {
        zcl_colorCtrlMoveToColorTemperatureCmd_t cmd = {0};
        cmd.colorTemperature = mireds;
        cmd.transitionTime = ttime;
        light_color_ctrl_cb(pAddrInfo, ZCL_CMD_LIGHT_COLOR_CONTROL_MOVE_TO_COLOR_TEMPERATURE, &cmd);
    } else if(mode == ZCL_COLOR_MODE_CURRENT_X_Y) {
        zcl_colorCtrlMoveToColorCmd_t cmd = {0};
        cmd.colorX = x;
        cmd.colorY = y;
        cmd.transitionTime = ttime;
        light_color_ctrl_cb(pAddrInfo, ZCL_CMD_LIGHT_COLOR_CONTROL_MOVE_TO_COLOR, &cmd);
    } else {
        zcl_colorCtrlEnhancedMoveToHueAndSaturationCmd_t cmd = {0};
        cmd.enhancedHue = hue;
        cmd.saturation = sat;
        //a loop takes over the hue ramp at once, so only fade when coming from white (that fade is a crossfade)
        cmd.transitionTime =
            (loop_active && p_color->color_mode != ZCL_COLOR_MODE_COLOR_TEMPERATURE_MIREDS) ? 0 : ttime;
        light_color_ctrl_cb(pAddrInfo, ZCL_CMD_LIGHT_COLOR_CONTROL_ENHANCED_MOVE_TO_HUE_AND_SATURATION, &cmd);

        if(loop_active) {
            zcl_colorCtrlColorLoopSetCmd_t loop = {0};
            loop.updateFlags.updateFlags = 0x07; //action, direction, time
            loop.action = COLOR_LOOP_SET_ACTION_FROM_ENHANCED_CURRENT_HUE;
            loop.direction = loop_direction ? COLOR_LOOP_SET_INCREMENT : COLOR_LOOP_SET_DECREMENT;
            loop.time = loop_time ? loop_time : p_color->color_loop_time;
            light_color_ctrl_cb(pAddrInfo, ZCL_CMD_LIGHT_COLOR_CONTROL_COLOR_LOOP_SET, &loop);
        }
    }
}

static void light_scene_recall_req_handler(zclIncomingAddrInfo_t* pAddrInfo, zcl_sceneEntry_t* p_scene) {
    uint8_t* p_data = p_scene->extField;
    uint8_t* p_end = p_scene->extField + p_scene->extFieldLen;
    uint16_t ttime = p_scene->transTime * 10 + p_scene->transTime100ms;

    while(p_data + 3 <= p_end) {
        uint16_t cluster_id = BUILD_U16(p_data[0], p_data[1]);
        uint8_t ext_len = p_data[2];
        p_data += 3;
        if(p_data + ext_len > p_end) {
            ext_len = (uint8_t)(p_end - p_data); //the stack truncates long extension fields: use what's there
        }

        if(cluster_id == ZCL_CLUSTER_GEN_ON_OFF) {
            if(ext_len >= 1) {
                light_on_off_cb(pAddrInfo, p_data[0], NULL);
            }
        } else if(cluster_id == ZCL_CLUSTER_GEN_LEVEL_CONTROL) {
            if(ext_len >= 1) {
                moveToLvl_t move_to_level = {0};
                move_to_level.optPresent = 0;
                move_to_level.level = p_data[0];
                move_to_level.transitionTime = ttime;
                light_level_cb(pAddrInfo, ZCL_CMD_LEVEL_MOVE_TO_LEVEL, &move_to_level);
            }
        } else if(cluster_id == ZCL_CLUSTER_LIGHTING_COLOR_CONTROL) {
            light_scene_recall_color(pAddrInfo, p_data, ext_len, ttime);
        }

        p_data += ext_len;
    }
}

static void put_u16(uint8_t* p, uint16_t v) {
    p[0] = LO_UINT16(v);
    p[1] = HI_UINT16(v);
}

static void light_scene_store_req_handler(zcl_sceneEntry_t* p_scene) {
    uint8_t ext_len = 0;

    ZclOnOffAttr* p_on_off = ZCL_ONOFF_ATTR_GET();

    p_scene->extField[ext_len++] = LO_UINT16(ZCL_CLUSTER_GEN_ON_OFF);
    p_scene->extField[ext_len++] = HI_UINT16(ZCL_CLUSTER_GEN_ON_OFF);
    p_scene->extField[ext_len++] = sizeof(uint8_t);
    p_scene->extField[ext_len++] = p_on_off->on_off;

    ZclLevelAttr* p_level = ZCL_LEVEL_ATTR_GET();

    p_scene->extField[ext_len++] = LO_UINT16(ZCL_CLUSTER_GEN_LEVEL_CONTROL);
    p_scene->extField[ext_len++] = HI_UINT16(ZCL_CLUSTER_GEN_LEVEL_CONTROL);
    p_scene->extField[ext_len++] = sizeof(uint8_t);
    p_scene->extField[ext_len++] = p_level->cur_level;

    ZclLightColorCtrlAttr* p_color = ZCL_COLOR_ATTR_GET();

    p_scene->extField[ext_len++] = LO_UINT16(ZCL_CLUSTER_LIGHTING_COLOR_CONTROL);
    p_scene->extField[ext_len++] = HI_UINT16(ZCL_CLUSTER_LIGHTING_COLOR_CONTROL);
    p_scene->extField[ext_len++] = COLOR_EXT_LEN;

    /* Only the active mode's fields are filled, so recall can tell the modes apart */
    uint8_t* c = &p_scene->extField[ext_len];
    memset(c, 0, COLOR_EXT_LEN);

    if(p_color->color_mode == ZCL_COLOR_MODE_COLOR_TEMPERATURE_MIREDS) {
        put_u16(c + 11, p_color->color_temperature_mireds);
    } else if(p_color->color_mode == ZCL_COLOR_MODE_CURRENT_X_Y ||
              (p_color->enhanced_current_hue == 0 && p_color->current_saturation == 0 && !p_color->color_loop_active)) {
        //XY mode, or a hue/saturation of exactly 0/0 that would read back as "no colour": store the lit colour as xy
        uint16_t x = p_color->current_x, y = p_color->current_y;
        if(p_color->color_mode != ZCL_COLOR_MODE_CURRENT_X_Y) {
            uint16_t r, g, b;
            light_color_shown_rgb(&r, &g, &b);
            color_rgb_to_xy(r, g, b, &x, &y);
        }
        put_u16(c + 0, x);
        put_u16(c + 2, y);
    } else {
        put_u16(c + 4, p_color->color_loop_active ? p_color->color_loop_stored_enhanced_hue : p_color->enhanced_current_hue);
        c[6] = p_color->current_saturation;
        c[7] = p_color->color_loop_active;
        c[8] = p_color->color_loop_direction;
        put_u16(c + 9, p_color->color_loop_time);
    }
    ext_len += COLOR_EXT_LEN;

    p_scene->extFieldLen = ext_len;
}

status_t light_scene_cb(zclIncomingAddrInfo_t* pAddrInfo, uint8_t cmd_id, void* cmd_payload) {
    status_t status = ZCL_STA_SUCCESS;

    if(pAddrInfo->dstEp == LIGHT_ENDPOINT) {
        if(pAddrInfo->dirCluster == ZCL_FRAME_CLIENT_SERVER_DIR) {
            switch(cmd_id) {
            case ZCL_CMD_SCENE_STORE_SCENE:
                light_scene_store_req_handler((zcl_sceneEntry_t*)cmd_payload);
                break;
            case ZCL_CMD_SCENE_RECALL_SCENE:
                light_scene_recall_req_handler(pAddrInfo, (zcl_sceneEntry_t*)cmd_payload);
                break;
            default:
                status = ZCL_STA_UNSUP_MANU_CLUSTER_COMMAND;
                break;
            }
        }
    }

    return status;
}
