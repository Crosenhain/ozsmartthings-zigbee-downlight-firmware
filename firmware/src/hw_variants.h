/*
 * Modified from nminaylov/zigbee-light-cct (Apache-2.0), 2026: DL41 hardware variants. See NOTICE.
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once
#include "hw_config.h"

// Oz Smart Things DL41-03-10-R-ZB RGBCW downlight: Tuya ZTU + SM2235EGH
// Values from the stock Tuya config at 0xF800A: iicscl:4 (PB4), iicsda:11 (PC3),
// 2235ccur:3, 2235wcur:3, iicr:0 iicg:1 iicb:2 iicc:3 iicw:4.
#define HW_VARIANT_DL41 &hw_config_dl41
static const HwConfig hw_config_dl41 = {
    .scl_pin = GPIO_PB4,
    .sda_pin = GPIO_PC3,
    .cur_rgb_code = 3, /* 16 mA */
    .cur_cw_code = 3,  /* 20 mA */
    .out_map = {
        [LIGHT_CH_R] = 0,
        [LIGHT_CH_G] = 1,
        [LIGHT_CH_B] = 2,
        [LIGHT_CH_C] = 3,
        [LIGHT_CH_W] = 4,
    },

    /* Nominal 2835 CW/WW bins; refine after measuring. */
    .cold_temp_k = 6000,
    .warm_temp_k = 3000,

    .status_led_pin = PIN_NC,
    .button_pin = PIN_NC,

    .pin_invert_mask = 0,

    .model_name = {.string = "DL41-RGBCW"},
};

// Bench test: ZTU module only, nothing driven
#define HW_VARIANT_DL41_TEST &hw_config_dl41_test
static const HwConfig hw_config_dl41_test = {
    .scl_pin = PIN_NC,
    .sda_pin = PIN_NC,
    .cur_rgb_code = 0,
    .cur_cw_code = 0,
    .out_map = {0, 1, 2, 3, 4},

    .cold_temp_k = 5500,
    .warm_temp_k = 4000,

    .status_led_pin = PIN_NC,
    .button_pin = PIN_NC,

    .pin_invert_mask = 0,

    .model_name = {.string = "DL41-CUSTOM-TEST"},
};

#define HW_VARIANT_NONE NULL
