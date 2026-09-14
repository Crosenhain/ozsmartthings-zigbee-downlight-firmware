/*
 * Modified from nminaylov/zigbee-light-cct (Apache-2.0), 2026: colour control attributes (HS, enhanced
 * hue, XY, colour loop), extended NV record, decimal software build ID. See NOTICE.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tl_common.h"
#include "zcl_include.h"
#include "app.h"
#include "hw_config.h"

#ifndef ZCL_BASIC_MFG_NAME
#define ZCL_BASIC_MFG_NAME {6, 'C', 'u', 's', 't', 'o', 'm'}
#endif

#ifndef ZCL_BASIC_SW_BUILD_ID
/* "v1.0.NN" with the build number in decimal (two digits minimum, three from 100).
 * Upstream printed APP_BUILD's hex nibbles, so build 10 read "v1.0.0:". */
#define SW_BUILD_ID_3DIGIT (APP_BUILD >= 100)
#define ZCL_BASIC_SW_BUILD_ID                                                   \
    {7 + SW_BUILD_ID_3DIGIT,                                                    \
     'v',                                                                       \
     (APP_RELEASE >> 4) + '0',                                                  \
     '.',                                                                       \
     (APP_RELEASE & 0xf) + '0',                                                 \
     '.',                                                                       \
     SW_BUILD_ID_3DIGIT ? (APP_BUILD / 100) + '0' : (APP_BUILD / 10) + '0',     \
     SW_BUILD_ID_3DIGIT ? (APP_BUILD / 10 % 10) + '0' : (APP_BUILD % 10) + '0', \
     SW_BUILD_ID_3DIGIT ? (APP_BUILD % 10) + '0' : 0}
#endif

#define COLOR_CAPABILITIES                                                                       \
    (ZCL_COLOR_CAPABILITIES_BIT_HUE_SATURATION | ZCL_COLOR_CAPABILITIES_BIT_ENHANCED_HUE |       \
     ZCL_COLOR_CAPABILITIES_BIT_COLOR_LOOP | ZCL_COLOR_CAPABILITIES_BIT_X_Y_ATTRIBUTES |          \
     ZCL_COLOR_CAPABILITIES_BIT_COLOR_TEMPERATURE)
#define COLOR_MODE_INIT    ZCL_COLOR_MODE_COLOR_TEMPERATURE_MIREDS

const uint16_t light_in_cluster_list[] = {
    ZCL_CLUSTER_GEN_BASIC,
    ZCL_CLUSTER_GEN_IDENTIFY,
    ZCL_CLUSTER_GEN_GROUPS,
    ZCL_CLUSTER_GEN_SCENES,
    ZCL_CLUSTER_GEN_ON_OFF,
    ZCL_CLUSTER_GEN_LEVEL_CONTROL,
    ZCL_CLUSTER_LIGHTING_COLOR_CONTROL,
    ZCL_CLUSTER_TOUCHLINK_COMMISSIONING,
#ifdef ZCL_WWAH
    ZCL_CLUSTER_WWAH,
#endif
};

const uint16_t light_out_cluster_list[] = {
    ZCL_CLUSTER_OTA,
};

const af_simple_descriptor_t light_simple_desc = {
    HA_PROFILE_ID, /* Application profile identifier */
    HA_DEV_COLOR_DIMMABLE_LIGHT, /* Application device identifier */
    LIGHT_ENDPOINT, /* Endpoint */
    1, /* Application device version */
    0, /* Reserved */
    ARRAY_SIZE(light_in_cluster_list), /* Application input cluster count */
    ARRAY_SIZE(light_out_cluster_list), /* Application output cluster count */
    (uint16_t*)light_in_cluster_list, /* Application input cluster list */
    (uint16_t*)light_out_cluster_list, /* Application output cluster list */
};

ZclBasicAttr zcl_basic_attrs = {
    .zcl_version = 0x03,
    .app_version = 0x00,
    .stack_version = 0x02,
    .hw_version = 0x00,
    .manu_name = ZCL_BASIC_MFG_NAME,
    // .model_id = ZCL_BASIC_MODEL_ID, // Set in zcl_light_attrs_init
    .power_source = POWER_SOURCE_MAINS_1_PHASE,
    .sw_build_id = ZCL_BASIC_SW_BUILD_ID,
    .device_enable = true,
};

/* clang-format off */
static const zclAttrInfo_t basic_attr_tbl[] = {
    {ZCL_ATTRID_BASIC_ZCL_VER, ZCL_DATA_TYPE_UINT8, ACCESS_CONTROL_READ, (uint8_t*)&zcl_basic_attrs.zcl_version},
    {ZCL_ATTRID_BASIC_APP_VER, ZCL_DATA_TYPE_UINT8, ACCESS_CONTROL_READ, (uint8_t*)&zcl_basic_attrs.app_version},
    {ZCL_ATTRID_BASIC_STACK_VER, ZCL_DATA_TYPE_UINT8, ACCESS_CONTROL_READ, (uint8_t*)&zcl_basic_attrs.stack_version},
    {ZCL_ATTRID_BASIC_HW_VER, ZCL_DATA_TYPE_UINT8, ACCESS_CONTROL_READ, (uint8_t*)&zcl_basic_attrs.hw_version},
    {ZCL_ATTRID_BASIC_MFR_NAME, ZCL_DATA_TYPE_CHAR_STR, ACCESS_CONTROL_READ, (uint8_t*)zcl_basic_attrs.manu_name},
    {ZCL_ATTRID_BASIC_MODEL_ID, ZCL_DATA_TYPE_CHAR_STR, ACCESS_CONTROL_READ, (uint8_t*)zcl_basic_attrs.model_id},
    {ZCL_ATTRID_BASIC_POWER_SOURCE, ZCL_DATA_TYPE_ENUM8, ACCESS_CONTROL_READ, (uint8_t*)&zcl_basic_attrs.power_source},
    {ZCL_ATTRID_BASIC_DEV_ENABLED, ZCL_DATA_TYPE_BOOLEAN, ACCESS_CONTROL_READ | ACCESS_CONTROL_WRITE, (uint8_t*)&zcl_basic_attrs.device_enable},
    {ZCL_ATTRID_BASIC_SW_BUILD_ID, ZCL_DATA_TYPE_CHAR_STR, ACCESS_CONTROL_READ, (uint8_t*)zcl_basic_attrs.sw_build_id},

    {ZCL_ATTRID_GLOBAL_CLUSTER_REVISION, ZCL_DATA_TYPE_UINT16, ACCESS_CONTROL_READ, (uint8_t*)&zcl_attr_global_clusterRevision},
};

ZclIdentifyAttr zcl_identify_attrs = {
    .identify_time = 0x0000,
};

static const zclAttrInfo_t identify_attr_tbl[] = {
    {ZCL_ATTRID_IDENTIFY_TIME, ZCL_DATA_TYPE_UINT16, ACCESS_CONTROL_READ | ACCESS_CONTROL_WRITE, (uint8_t*)&zcl_identify_attrs.identify_time},

    {ZCL_ATTRID_GLOBAL_CLUSTER_REVISION, ZCL_DATA_TYPE_UINT16, ACCESS_CONTROL_READ, (uint8_t*)&zcl_attr_global_clusterRevision},
};

ZclGroupAttr zcl_group_attrs = {
    .name_support = 0,
};

static const zclAttrInfo_t group_attr_tbl[] = {
    {ZCL_ATTRID_GROUP_NAME_SUPPORT, ZCL_DATA_TYPE_BITMAP8, ACCESS_CONTROL_READ, (uint8_t*)&zcl_group_attrs.name_support},

    {ZCL_ATTRID_GLOBAL_CLUSTER_REVISION, ZCL_DATA_TYPE_UINT16, ACCESS_CONTROL_READ, (uint8_t*)&zcl_attr_global_clusterRevision},
};

ZclSceneAttr zcl_scene_attrs = {
    .scene_count = 0,
    .current_scene = 0,
    .current_group = 0x0000,
    .scene_valid = false,
    .name_support = 0,
};

static const zclAttrInfo_t scene_attr_tbl[] = {
    {ZCL_ATTRID_SCENE_SCENE_COUNT, ZCL_DATA_TYPE_UINT8, ACCESS_CONTROL_READ, (uint8_t*)&zcl_scene_attrs.scene_count},
    {ZCL_ATTRID_SCENE_CURRENT_SCENE, ZCL_DATA_TYPE_UINT8, ACCESS_CONTROL_READ, (uint8_t*)&zcl_scene_attrs.current_scene},
    {ZCL_ATTRID_SCENE_CURRENT_GROUP, ZCL_DATA_TYPE_UINT16, ACCESS_CONTROL_READ, (uint8_t*)&zcl_scene_attrs.current_group},
    {ZCL_ATTRID_SCENE_SCENE_VALID, ZCL_DATA_TYPE_BOOLEAN, ACCESS_CONTROL_READ, (uint8_t*)&zcl_scene_attrs.scene_valid},
    {ZCL_ATTRID_SCENE_NAME_SUPPORT, ZCL_DATA_TYPE_BITMAP8, ACCESS_CONTROL_READ, (uint8_t*)&zcl_scene_attrs.name_support},

    {ZCL_ATTRID_GLOBAL_CLUSTER_REVISION, ZCL_DATA_TYPE_UINT16, ACCESS_CONTROL_READ, (uint8_t*)&zcl_attr_global_clusterRevision},
};

ZclOnOffAttr zcl_on_off_attrs = {
    .on_off = 1,
    .global_scene_control = 1,
    .on_time = 0x0000,
    .off_wait_time = 0x0000,
    .start_up_on_off = ZCL_START_UP_ONOFF_SET_ONOFF_TO_PREVIOUS,
};

static const zclAttrInfo_t on_off_attr_tbl[] = {
    {ZCL_ATTRID_ONOFF, ZCL_DATA_TYPE_BOOLEAN, ACCESS_CONTROL_READ | ACCESS_CONTROL_REPORTABLE, (uint8_t*)&zcl_on_off_attrs.on_off},
    {ZCL_ATTRID_GLOBAL_SCENE_CONTROL, ZCL_DATA_TYPE_BOOLEAN, ACCESS_CONTROL_READ, (uint8_t*)&zcl_on_off_attrs.global_scene_control},
    {ZCL_ATTRID_ON_TIME, ZCL_DATA_TYPE_UINT16, ACCESS_CONTROL_READ | ACCESS_CONTROL_WRITE, (uint8_t*)&zcl_on_off_attrs.on_time},
    {ZCL_ATTRID_OFF_WAIT_TIME, ZCL_DATA_TYPE_UINT16, ACCESS_CONTROL_READ | ACCESS_CONTROL_WRITE, (uint8_t*)&zcl_on_off_attrs.off_wait_time},
    {ZCL_ATTRID_START_UP_ONOFF, ZCL_DATA_TYPE_ENUM8, ACCESS_CONTROL_READ | ACCESS_CONTROL_WRITE, (uint8_t*)&zcl_on_off_attrs.start_up_on_off},

    {ZCL_ATTRID_GLOBAL_CLUSTER_REVISION, ZCL_DATA_TYPE_UINT16, ACCESS_CONTROL_READ, (uint8_t*)&zcl_attr_global_clusterRevision},
};

ZclLevelAttr zcl_level_attrs = {
    .cur_level = ZCL_LEVEL_ATTR_MAX_LEVEL / 2, // 50%
    .remaining_time = 0,
    .on_off_transition_time = 3, // 300ms
    .options = 0,
    .min_level = ZCL_LEVEL_ATTR_MIN_LEVEL,
    .max_level = ZCL_LEVEL_ATTR_MAX_LEVEL,
    .start_up_current_level = ZCL_START_UP_CURRENT_LEVEL_TO_PREVIOUS,
};

static const zclAttrInfo_t level_attr_tbl[] = {
    {ZCL_ATTRID_LEVEL_CURRENT_LEVEL, ZCL_DATA_TYPE_UINT8, ACCESS_CONTROL_READ | ACCESS_CONTROL_REPORTABLE, (uint8_t*)&zcl_level_attrs.cur_level},
    {ZCL_ATTRID_LEVEL_REMAINING_TIME, ZCL_DATA_TYPE_UINT16, ACCESS_CONTROL_READ, (uint8_t*)&zcl_level_attrs.remaining_time},
    {ZCL_ATTRID_LEVEL_MIN_LEVEL, ZCL_DATA_TYPE_UINT8, ACCESS_CONTROL_READ, (uint8_t*)&zcl_level_attrs.min_level},
    {ZCL_ATTRID_LEVEL_MAX_LEVEL, ZCL_DATA_TYPE_UINT8, ACCESS_CONTROL_READ, (uint8_t*)&zcl_level_attrs.max_level},
    {ZCL_ATTRID_LEVEL_ON_OFF_TRANSITION_TIME, ZCL_DATA_TYPE_UINT16, ACCESS_CONTROL_READ | ACCESS_CONTROL_WRITE, (uint8_t*)&zcl_level_attrs.on_off_transition_time},
    {ZCL_ATTRID_LEVEL_OPTIONS, ZCL_DATA_TYPE_BITMAP8, ACCESS_CONTROL_READ | ACCESS_CONTROL_WRITE, (uint8_t*)&zcl_level_attrs.options},
    // {ZCL_ATTRID_LEVEL_START_UP_CURRENT_LEVEL, ZCL_DATA_TYPE_UINT8, ACCESS_CONTROL_READ | ACCESS_CONTROL_WRITE, (uint8_t*)&zcl_level_attrs.start_up_current_level},

    {ZCL_ATTRID_GLOBAL_CLUSTER_REVISION, ZCL_DATA_TYPE_UINT16, ACCESS_CONTROL_READ, (uint8_t*)&zcl_attr_global_clusterRevision},
};

ZclLightColorCtrlAttr zcl_color_ctrl_attrs = {
    .color_capabilities = COLOR_CAPABILITIES,
    .color_mode = COLOR_MODE_INIT,
    .enhanced_color_mode = COLOR_MODE_INIT,
    .options = 0,
    .num_of_primaries = 0,
    // .color_temperature_mireds = (1000000 / ((COLOR_TEMPERATURE_MIN + COLOR_TEMPERATURE_MAX) / 2)),
    // .color_temp_physical_min_mireds = (1000000 / COLOR_TEMPERATURE_MIN),
    // .color_temp_physical_max_mireds = (1000000 / COLOR_TEMPERATURE_MAX),
    // .couple_color_temp_to_level_min_mireds = (1000000 / COLOR_TEMPERATURE_MIN),
    .start_up_color_temperature_mireds = ZCL_START_UP_COLOR_TEMPERATURE_MIREDS_TO_PREVIOUS,
    .current_hue = 0,
    .current_saturation = 0,
    .enhanced_current_hue = 0,
    .current_x = 0x616B, /* default white point, as the Telink sampleLight uses */
    .current_y = 0x607D,
    .color_loop_active = 0,
    .color_loop_direction = 0,
    .color_loop_time = 0x0019,               /* ZCL default: 25 s */
    .color_loop_start_enhanced_hue = 0x2300, /* ZCL default */
    .color_loop_stored_enhanced_hue = 0,
};

static const zclAttrInfo_t light_color_ctrl_attr_tbl[] = {
    {ZCL_ATTRID_CURRENT_HUE, ZCL_DATA_TYPE_UINT8, ACCESS_CONTROL_READ | ACCESS_CONTROL_REPORTABLE, (uint8_t*)&zcl_color_ctrl_attrs.current_hue},
    {ZCL_ATTRID_CURRENT_SATURATION, ZCL_DATA_TYPE_UINT8, ACCESS_CONTROL_READ | ACCESS_CONTROL_REPORTABLE, (uint8_t*)&zcl_color_ctrl_attrs.current_saturation},
    {ZCL_ATTRID_CURRENT_X, ZCL_DATA_TYPE_UINT16, ACCESS_CONTROL_READ | ACCESS_CONTROL_REPORTABLE, (uint8_t*)&zcl_color_ctrl_attrs.current_x},
    {ZCL_ATTRID_CURRENT_Y, ZCL_DATA_TYPE_UINT16, ACCESS_CONTROL_READ | ACCESS_CONTROL_REPORTABLE, (uint8_t*)&zcl_color_ctrl_attrs.current_y},
    {ZCL_ATTRID_ENHANCED_CURRENT_HUE, ZCL_DATA_TYPE_UINT16, ACCESS_CONTROL_READ | ACCESS_CONTROL_REPORTABLE, (uint8_t*)&zcl_color_ctrl_attrs.enhanced_current_hue},
    {ZCL_ATTRID_COLOR_LOOP_ACTIVE, ZCL_DATA_TYPE_UINT8, ACCESS_CONTROL_READ | ACCESS_CONTROL_REPORTABLE, (uint8_t*)&zcl_color_ctrl_attrs.color_loop_active},
    {ZCL_ATTRID_COLOR_LOOP_DIRECTION, ZCL_DATA_TYPE_UINT8, ACCESS_CONTROL_READ, (uint8_t*)&zcl_color_ctrl_attrs.color_loop_direction},
    {ZCL_ATTRID_COLOR_LOOP_TIME, ZCL_DATA_TYPE_UINT16, ACCESS_CONTROL_READ, (uint8_t*)&zcl_color_ctrl_attrs.color_loop_time},
    {ZCL_ATTRID_COLOR_LOOP_START_ENHANCED_HUE, ZCL_DATA_TYPE_UINT16, ACCESS_CONTROL_READ, (uint8_t*)&zcl_color_ctrl_attrs.color_loop_start_enhanced_hue},
    {ZCL_ATTRID_COLOR_LOOP_STORED_ENHANCED_HUE, ZCL_DATA_TYPE_UINT16, ACCESS_CONTROL_READ, (uint8_t*)&zcl_color_ctrl_attrs.color_loop_stored_enhanced_hue},
    {ZCL_ATTRID_COLOR_MODE, ZCL_DATA_TYPE_ENUM8, ACCESS_CONTROL_READ, (uint8_t*)&zcl_color_ctrl_attrs.color_mode},
    {ZCL_ATTRID_COLOR_OPTIONS, ZCL_DATA_TYPE_BITMAP8, ACCESS_CONTROL_READ | ACCESS_CONTROL_WRITE, (uint8_t*)&zcl_color_ctrl_attrs.options},
    {ZCL_ATTRID_ENHANCED_COLOR_MODE, ZCL_DATA_TYPE_ENUM8, ACCESS_CONTROL_READ, (uint8_t*)&zcl_color_ctrl_attrs.enhanced_color_mode},
    {ZCL_ATTRID_COLOR_CAPABILITIES, ZCL_DATA_TYPE_BITMAP16, ACCESS_CONTROL_READ, (uint8_t*)&zcl_color_ctrl_attrs.color_capabilities},
    {ZCL_ATTRID_NUMBER_OF_PRIMARIES, ZCL_DATA_TYPE_UINT8, ACCESS_CONTROL_READ, (uint8_t*)&zcl_color_ctrl_attrs.num_of_primaries},

    {ZCL_ATTRID_COLOR_TEMPERATURE_MIREDS, ZCL_DATA_TYPE_UINT16, ACCESS_CONTROL_READ | ACCESS_CONTROL_REPORTABLE, (uint8_t*)&zcl_color_ctrl_attrs.color_temperature_mireds},
    {ZCL_ATTRID_COLOR_TEMP_PHYSICAL_MIN_MIREDS, ZCL_DATA_TYPE_UINT16, ACCESS_CONTROL_READ, (uint8_t*)&zcl_color_ctrl_attrs.color_temp_physical_min_mireds},
    {ZCL_ATTRID_COLOR_TEMP_PHYSICAL_MAX_MIREDS, ZCL_DATA_TYPE_UINT16, ACCESS_CONTROL_READ, (uint8_t*)&zcl_color_ctrl_attrs.color_temp_physical_max_mireds},
    {ZCL_ATTRID_COUPLE_COLOR_TEMP_TO_LEVEL_MIN_MIREDS, ZCL_DATA_TYPE_UINT16, ACCESS_CONTROL_READ, (uint8_t*)&zcl_color_ctrl_attrs.couple_color_temp_to_level_min_mireds},
    // {ZCL_ATTRID_START_UP_COLOR_TEMPERATURE_MIREDS, ZCL_DATA_TYPE_UINT16, ACCESS_CONTROL_READ | ACCESS_CONTROL_WRITE, (uint8_t*)&zcl_color_ctrl_attrs.start_up_color_temperature_mireds},
    
    {ZCL_ATTRID_GLOBAL_CLUSTER_REVISION, ZCL_DATA_TYPE_UINT16, ACCESS_CONTROL_READ, (uint8_t*)&zcl_attr_global_clusterRevision},
};

const zcl_specClusterInfo_t light_cluster_list[] = {
    {ZCL_CLUSTER_GEN_BASIC, MANUFACTURER_CODE_NONE, ARRAY_SIZE(basic_attr_tbl), basic_attr_tbl, zcl_basic_register, light_basic_cb},
    {ZCL_CLUSTER_GEN_IDENTIFY, MANUFACTURER_CODE_NONE, ARRAY_SIZE(identify_attr_tbl), identify_attr_tbl, zcl_identify_register, light_identify_cb},
    {ZCL_CLUSTER_GEN_GROUPS, MANUFACTURER_CODE_NONE, ARRAY_SIZE(group_attr_tbl), group_attr_tbl, zcl_group_register, NULL},
    {ZCL_CLUSTER_GEN_SCENES, MANUFACTURER_CODE_NONE, ARRAY_SIZE(scene_attr_tbl), scene_attr_tbl, zcl_scene_register, light_scene_cb},
    {ZCL_CLUSTER_GEN_ON_OFF, MANUFACTURER_CODE_NONE, ARRAY_SIZE(on_off_attr_tbl), on_off_attr_tbl, zcl_onOff_register, light_on_off_cb},
    {ZCL_CLUSTER_GEN_LEVEL_CONTROL, MANUFACTURER_CODE_NONE, ARRAY_SIZE(level_attr_tbl), level_attr_tbl, zcl_level_register, light_level_cb},
    {ZCL_CLUSTER_LIGHTING_COLOR_CONTROL, MANUFACTURER_CODE_NONE, ARRAY_SIZE(light_color_ctrl_attr_tbl), light_color_ctrl_attr_tbl, zcl_lightColorCtrl_register, light_color_ctrl_cb},
};
uint8_t light_cb_cluster_num = (sizeof(light_cluster_list) / sizeof(light_cluster_list[0]));

/* clang-format on */

nv_sts_t zcl_on_off_attr_save(void) {
    nv_sts_t st = NV_SUCC;
    bool need_save = false;
    ZclNvOnOff zcl_nv_on_off;

    st = nv_flashReadNew(1, NV_MODULE_ZCL, NV_ITEM_ZCL_ON_OFF, sizeof(ZclNvOnOff), (uint8_t*)&zcl_nv_on_off);
    if(st == NV_SUCC) {
        if((zcl_nv_on_off.on_off != zcl_on_off_attrs.on_off) ||
           (zcl_nv_on_off.start_up_on_off != zcl_on_off_attrs.start_up_on_off)) {
            need_save = true;
        }
    } else if(st == NV_ITEM_NOT_FOUND) {
        need_save = true;
    }

    if(need_save) {
        zcl_nv_on_off.on_off = zcl_on_off_attrs.on_off;
        zcl_nv_on_off.start_up_on_off = zcl_on_off_attrs.start_up_on_off;

        st = nv_flashWriteNew(1, NV_MODULE_ZCL, NV_ITEM_ZCL_ON_OFF, sizeof(ZclNvOnOff), (uint8_t*)&zcl_nv_on_off);
    }
    return st;
}

static nv_sts_t zcl_on_off_attr_restore(void) {
    nv_sts_t st = NV_SUCC;
    ZclNvOnOff zcl_nv_on_off;

    st = nv_flashReadNew(1, NV_MODULE_ZCL, NV_ITEM_ZCL_ON_OFF, sizeof(ZclNvOnOff), (uint8_t*)&zcl_nv_on_off);
    if(st == NV_SUCC) {
        zcl_on_off_attrs.on_off = zcl_nv_on_off.on_off;
        zcl_on_off_attrs.start_up_on_off = zcl_nv_on_off.start_up_on_off;
    }
    return st;
}

nv_sts_t zcl_level_attr_save(void) {
    nv_sts_t st = NV_SUCC;
    bool need_save = false;
    ZclNvLevel zcl_nv_level;

    st = nv_flashReadNew(1, NV_MODULE_ZCL, NV_ITEM_ZCL_LEVEL, sizeof(ZclNvLevel), (uint8_t*)&zcl_nv_level);
    if(st == NV_SUCC) {
        if((zcl_nv_level.cur_level != zcl_level_attrs.cur_level) ||
           (zcl_nv_level.start_up_cur_level != zcl_level_attrs.start_up_current_level) ||
           (zcl_nv_level.on_off_transition_time != zcl_level_attrs.on_off_transition_time)) {
            need_save = true;
        }
    } else if(st == NV_ITEM_NOT_FOUND) {
        need_save = true;
    }

    if(need_save) {
        zcl_nv_level.cur_level = zcl_level_attrs.cur_level;
        zcl_nv_level.start_up_cur_level = zcl_level_attrs.start_up_current_level;
        zcl_nv_level.on_off_transition_time = zcl_level_attrs.on_off_transition_time;

        st = nv_flashWriteNew(1, NV_MODULE_ZCL, NV_ITEM_ZCL_LEVEL, sizeof(ZclNvLevel), (uint8_t*)&zcl_nv_level);
    }
    return st;
}

static nv_sts_t zcl_level_attr_restore(void) {
    nv_sts_t st = NV_SUCC;
    ZclNvLevel zcl_nv_level;

    st = nv_flashReadNew(1, NV_MODULE_ZCL, NV_ITEM_ZCL_LEVEL, sizeof(ZclNvLevel), (uint8_t*)&zcl_nv_level);
    if(st == NV_SUCC) {
        zcl_level_attrs.cur_level = zcl_nv_level.cur_level;
        zcl_level_attrs.start_up_current_level = zcl_nv_level.start_up_cur_level;
        zcl_level_attrs.on_off_transition_time = zcl_nv_level.on_off_transition_time;
    }
    return st;
}

nv_sts_t zcl_color_ctrl_attr_save(void) {
    nv_sts_t st = NV_SUCC;
    bool need_save = false;
    ZclNvColorCtrl zcl_nv_color_ctrl;

    st =
        nv_flashReadNew(1, NV_MODULE_ZCL, NV_ITEM_ZCL_COLOR_CTRL, sizeof(ZclNvColorCtrl), (uint8_t*)&zcl_nv_color_ctrl);
    if(st == NV_SUCC) {
        if((zcl_nv_color_ctrl.color_temperature_mireds != zcl_color_ctrl_attrs.color_temperature_mireds) ||
           (zcl_nv_color_ctrl.start_up_color_temperature_mireds !=
            zcl_color_ctrl_attrs.start_up_color_temperature_mireds) ||
           (zcl_nv_color_ctrl.color_mode != zcl_color_ctrl_attrs.color_mode) ||
           (zcl_nv_color_ctrl.enhanced_color_mode != zcl_color_ctrl_attrs.enhanced_color_mode) ||
           (zcl_nv_color_ctrl.enhanced_current_hue != zcl_color_ctrl_attrs.enhanced_current_hue) ||
           (zcl_nv_color_ctrl.current_saturation != zcl_color_ctrl_attrs.current_saturation) ||
           (zcl_nv_color_ctrl.current_x != zcl_color_ctrl_attrs.current_x) ||
           (zcl_nv_color_ctrl.current_y != zcl_color_ctrl_attrs.current_y)) {
            need_save = true;
        }
    } else {
        /* Not found, or stored by an older build with a shorter record (pre-colour 1.0.05). */
        need_save = true;
    }

    if(need_save) {
        zcl_nv_color_ctrl.color_temperature_mireds = zcl_color_ctrl_attrs.color_temperature_mireds;
        zcl_nv_color_ctrl.start_up_color_temperature_mireds = zcl_color_ctrl_attrs.start_up_color_temperature_mireds;
        zcl_nv_color_ctrl.color_mode = zcl_color_ctrl_attrs.color_mode;
        zcl_nv_color_ctrl.enhanced_color_mode = zcl_color_ctrl_attrs.enhanced_color_mode;
        zcl_nv_color_ctrl.enhanced_current_hue = zcl_color_ctrl_attrs.enhanced_current_hue;
        zcl_nv_color_ctrl.current_saturation = zcl_color_ctrl_attrs.current_saturation;
        zcl_nv_color_ctrl.current_x = zcl_color_ctrl_attrs.current_x;
        zcl_nv_color_ctrl.current_y = zcl_color_ctrl_attrs.current_y;

        st = nv_flashWriteNew(
            1, NV_MODULE_ZCL, NV_ITEM_ZCL_COLOR_CTRL, sizeof(ZclNvColorCtrl), (uint8_t*)&zcl_nv_color_ctrl);
    }
    return st;
}

static nv_sts_t zcl_color_ctrl_attr_restore(void) {
    nv_sts_t st = NV_SUCC;
    ZclNvColorCtrl zcl_nv_color_ctrl;

    st =
        nv_flashReadNew(1, NV_MODULE_ZCL, NV_ITEM_ZCL_COLOR_CTRL, sizeof(ZclNvColorCtrl), (uint8_t*)&zcl_nv_color_ctrl);
    if(st == NV_SUCC) {
        zcl_color_ctrl_attrs.color_mode = zcl_nv_color_ctrl.color_mode;
        zcl_color_ctrl_attrs.enhanced_color_mode = zcl_nv_color_ctrl.enhanced_color_mode;
        zcl_color_ctrl_attrs.color_temperature_mireds = zcl_nv_color_ctrl.color_temperature_mireds;
        zcl_color_ctrl_attrs.start_up_color_temperature_mireds = zcl_nv_color_ctrl.start_up_color_temperature_mireds;
        zcl_color_ctrl_attrs.enhanced_current_hue = zcl_nv_color_ctrl.enhanced_current_hue;
        zcl_color_ctrl_attrs.current_saturation = zcl_nv_color_ctrl.current_saturation;
        zcl_color_ctrl_attrs.current_x = zcl_nv_color_ctrl.current_x;
        zcl_color_ctrl_attrs.current_y = zcl_nv_color_ctrl.current_y;
    }
    return st;
}

void zcl_light_attrs_init(void) {
    HwConfig* hw_cfg = hw_config_get();

    uint8_t* model_name = (uint8_t*)&(hw_cfg->model_name);
    memcpy(zcl_basic_attrs.model_id, model_name, sizeof(zcl_basic_attrs.model_id));

    zcl_color_ctrl_attrs.color_temperature_mireds = (1000000 / ((hw_cfg->cold_temp_k + hw_cfg->warm_temp_k) / 2));
    zcl_color_ctrl_attrs.color_temp_physical_min_mireds = (1000000 / hw_cfg->cold_temp_k);
    zcl_color_ctrl_attrs.color_temp_physical_max_mireds = (1000000 / hw_cfg->warm_temp_k);
    zcl_color_ctrl_attrs.couple_color_temp_to_level_min_mireds = (1000000 / hw_cfg->cold_temp_k);

    zcl_on_off_attr_restore();
    zcl_level_attr_restore();
    zcl_color_ctrl_attr_restore();
}
