/*
 * Modified from nminaylov/zigbee-light-cct (Apache-2.0), 2026: colour, colour-loop and extended NV
 * attributes for the DL41 RGBCW downlight. See NOTICE.
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once
#include <stdint.h>

#define LIGHT_ENDPOINT 0x01

typedef struct {
    uint8_t key_type; /* ERTIFICATION_KEY or MASTER_KEY key for touch-link or distribute network
                         SS_UNIQUE_LINK_KEY or SS_GLOBAL_LINK_KEY for distribute network */
    uint8_t key[16]; /* the key used */
} AppLinkKeyInfo;

typedef struct {
    ev_timer_event_t* timer_led_evt;

    uint16_t led_on_time;
    uint16_t led_off_time;
    uint8_t ori_sta; //original state before blink
    uint8_t sta; //current state in blink
    uint8_t times; //blink times
    uint8_t state;
    bool bdb_find_bind_flg;
    bool light_attrs_changed;

    AppLinkKeyInfo tc_link_key;
} AppCtx;

typedef struct {
    uint8_t zcl_version;
    uint8_t app_version;
    uint8_t stack_version;
    uint8_t hw_version;
    uint8_t manu_name[ZCL_BASIC_MAX_LENGTH];
    uint8_t model_id[ZCL_BASIC_MAX_LENGTH];
    uint8_t sw_build_id[ZCL_BASIC_MAX_LENGTH];
    uint8_t power_source;
    uint8_t device_enable;
} ZclBasicAttr;

typedef struct {
    uint16_t identify_time;
} ZclIdentifyAttr;

typedef struct {
    uint8_t name_support;
} ZclGroupAttr;

typedef struct {
    uint8_t scene_count;
    uint8_t current_scene;
    uint8_t name_support;
    bool scene_valid;
    uint16_t current_group;
} ZclSceneAttr;

typedef struct {
    uint16_t on_time;
    uint16_t off_wait_time;
    uint8_t start_up_on_off;
    bool on_off;
    bool global_scene_control;
} ZclOnOffAttr;

typedef struct {
    uint16_t remaining_time;
    uint16_t on_off_transition_time;
    uint8_t cur_level;
    uint8_t start_up_current_level;
    uint8_t min_level;
    uint8_t max_level;
    uint8_t options;
} ZclLevelAttr;

typedef struct {
    uint8_t color_mode;
    uint8_t enhanced_color_mode;
    uint8_t options;
    uint8_t num_of_primaries;
    uint16_t color_capabilities;
    uint16_t color_temperature_mireds;
    uint16_t color_temp_physical_min_mireds;
    uint16_t color_temp_physical_max_mireds;
    uint16_t couple_color_temp_to_level_min_mireds;
    uint16_t start_up_color_temperature_mireds;
    /* DL41 colour support */
    uint8_t current_hue;           /* 0..254 */
    uint8_t current_saturation;    /* 0..254 */
    uint16_t enhanced_current_hue; /* 0..65535 */
    uint16_t current_x;            /* 0..65279 (x * 65536) */
    uint16_t current_y;
    /* Colour loop (ColorLoopSet); not persisted across power loss */
    uint8_t color_loop_active;
    uint8_t color_loop_direction;            /* 0 decrement, 1 increment */
    uint16_t color_loop_time;                /* seconds per full turn of the wheel */
    uint16_t color_loop_start_enhanced_hue;
    uint16_t color_loop_stored_enhanced_hue; /* hue restored when the loop is deactivated */
} ZclLightColorCtrlAttr;

typedef struct _attribute_packed_ {
    uint8_t on_off;
    uint8_t start_up_on_off;
} ZclNvOnOff;

typedef struct _attribute_packed_ {
    uint8_t cur_level;
    uint8_t start_up_cur_level;
    uint16_t on_off_transition_time;
} ZclNvLevel;

typedef struct _attribute_packed_ {
    uint8_t color_mode;
    uint8_t enhanced_color_mode;
    uint16_t color_temperature_mireds;
    uint16_t start_up_color_temperature_mireds;
    uint16_t enhanced_current_hue;
    uint8_t current_saturation;
    uint16_t current_x;
    uint16_t current_y;
} ZclNvColorCtrl;

extern AppCtx light_ctx;
extern bdb_appCb_t light_zb_bdb_cb;

extern uint8_t light_cb_cluster_num;
extern const zcl_specClusterInfo_t light_cluster_list[];
extern const af_simple_descriptor_t light_simple_desc;

extern ZclBasicAttr zcl_basic_attrs;
extern ZclIdentifyAttr zcl_identify_attrs;
extern ZclGroupAttr zcl_group_attrs;
extern ZclSceneAttr zcl_scene_attrs;
extern ZclOnOffAttr zcl_on_off_attrs;
extern ZclLevelAttr zcl_level_attrs;
extern ZclLightColorCtrlAttr zcl_color_ctrl_attrs;

#define ZCL_SCENE_ATTR_GET() &zcl_scene_attrs
#define ZCL_ONOFF_ATTR_GET() &zcl_on_off_attrs
#define ZCL_LEVEL_ATTR_GET() &zcl_level_attrs
#define ZCL_COLOR_ATTR_GET() &zcl_color_ctrl_attrs

void light_zclProcessIncomingMsg(zclIncoming_t* pInHdlrMsg);

status_t light_basic_cb(zclIncomingAddrInfo_t* pAddrInfo, uint8_t cmd_id, void* cmd_payload);
status_t light_identify_cb(zclIncomingAddrInfo_t* pAddrInfo, uint8_t cmd_id, void* cmd_payload);
status_t light_scene_cb(zclIncomingAddrInfo_t* pAddrInfo, uint8_t cmd_id, void* cmd_payload);
status_t light_on_off_cb(zclIncomingAddrInfo_t* pAddrInfo, uint8_t cmd_id, void* cmd_payload);
status_t light_level_cb(zclIncomingAddrInfo_t* pAddrInfo, uint8_t cmd_id, void* cmd_payload);
status_t light_color_ctrl_cb(zclIncomingAddrInfo_t* pAddrInfo, uint8_t cmd_id, void* cmd_payload);

void light_on_off_update(uint8_t cmd);

void light_on_off_init(void);
void light_color_init(void);
void light_level_init(void);
void light_zcl_identify_cmd_handler(uint8_t endpoint, uint16_t src_addr, uint16_t identify_time);

void zcl_light_attrs_init(void);
nv_sts_t zcl_on_off_attr_save(void);
nv_sts_t zcl_level_attr_save(void);
nv_sts_t zcl_color_ctrl_attr_save(void);
