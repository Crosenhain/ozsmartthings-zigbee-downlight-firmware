/* Host-side stub of the Telink ZCL surface used by app.h, hw_config.h and light_control.c. */
#pragma once
#include "tl_common.h"

typedef uint8_t status_t;
typedef uint8_t nv_sts_t;
typedef struct { int unused; } bdb_appCb_t;
typedef struct { int unused; } zcl_specClusterInfo_t;
typedef struct { int unused; } af_simple_descriptor_t;
typedef struct { int unused; } zclIncoming_t;
typedef struct { uint8_t dstEp; } zclIncomingAddrInfo_t;

#define ZCL_BASIC_MAX_LENGTH 24

#define ZCL_CMD_ONOFF_OFF    0x00
#define ZCL_CMD_ONOFF_ON     0x01
#define ZCL_CMD_ONOFF_TOGGLE 0x02
#define ZCL_ONOFF_STATUS_OFF 0
#define ZCL_ONOFF_STATUS_ON  1

#define ZCL_LEVEL_ATTR_MIN_LEVEL             0x01
#define ZCL_LEVEL_ATTR_MAX_LEVEL             0xFE
#define ZCL_LEVEL_OPTIONS_COUPLE_CT_TO_LEVEL 0x02

#define ZCL_COLOR_MODE_CURRENT_HUE_SATURATION          0x00
#define ZCL_COLOR_MODE_CURRENT_X_Y                     0x01
#define ZCL_COLOR_MODE_COLOR_TEMPERATURE_MIREDS        0x02
#define ZCL_ENHANCED_COLOR_MODE_CURRENT_HUE_SATURATION 0x03
