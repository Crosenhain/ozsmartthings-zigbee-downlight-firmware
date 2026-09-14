/*
 * Modified from nminaylov/zigbee-light-cct (Apache-2.0), 2026: hardware config v2 for an SM2235 LED
 * driver (I2C pins, current codes, channel map). See NOTICE.
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once
#include <stdint.h>
#include "tl_common.h"
#include "zcl_include.h"

#define HW_CONFIG_MAGIC   0xA5
#define HW_CONFIG_VERSION 0x02 /* v2: SM2235 I2C LED driver instead of PWM */

#define PIN_NC 0

/* Hard limits on SM2235 current codes. Stock DL41 firmware uses 3/3
 * (RGB 16 mA, CW 20 mA). Raise only after measuring LED current on real hardware. */
#define HW_CUR_CODE_LIMIT_RGB 3
#define HW_CUR_CODE_LIMIT_CW  3

enum {
    INVERT_LED_STATUS = (1 << 1),
    INVERT_BUTTON = (1 << 2),
};

/* Logical LED channels; out_map[channel] gives the SM2235 output index (0 = OUT1). */
enum {
    LIGHT_CH_R = 0,
    LIGHT_CH_G,
    LIGHT_CH_B,
    LIGHT_CH_C, /* cool white */
    LIGHT_CH_W, /* warm white */
    LIGHT_CH_COUNT,
};

typedef struct {
    uint8_t magic;
    uint8_t version;

    uint16_t scl_pin;
    uint16_t sda_pin;
    uint8_t cur_rgb_code; /* 0..15, RGB current 4*(n+1) mA */
    uint8_t cur_cw_code;  /* 0..15, CW current 5*(n+1) mA */
    uint8_t out_map[LIGHT_CH_COUNT];

    uint16_t cold_temp_k;
    uint16_t warm_temp_k;

    uint16_t status_led_pin;
    uint16_t button_pin;

    uint8_t pin_invert_mask;

    struct __attribute__((packed)) {
        uint8_t len;
        char string[ZCL_BASIC_MAX_LENGTH - 1];
    } model_name;

    uint32_t crc;
} HwConfig;

bool hw_config_init(void);

HwConfig* hw_config_get(void);
