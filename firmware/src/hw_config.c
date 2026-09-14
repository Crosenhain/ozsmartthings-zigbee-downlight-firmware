/*
 * Modified from nminaylov/zigbee-light-cct (Apache-2.0), 2026: hardware config v2 for an SM2235 LED
 * driver (I2C pins, current codes, channel map). See NOTICE.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "hw_config.h"
#include "hw_variants.h"

static const HwConfig* hw_config_set = HW_VARIANT;

// pre-install code is not used, so we can use this page to store the hardware configuration
#define HW_CONFIG_PAGE   (CFG_PRE_INSTALL_CODE)
#define HW_CONFIG_OFFSET (256)
#define HW_CONFIG_ADDR   (HW_CONFIG_PAGE + HW_CONFIG_OFFSET)

static const HwConfig hw_config_default = {
    .scl_pin = PIN_NC, /* driver disabled */
    .sda_pin = PIN_NC,
    .cur_rgb_code = 0,
    .cur_cw_code = 0,
    .out_map = {0, 1, 2, 3, 4},
    .cold_temp_k = 6500,
    .warm_temp_k = 2700,
    .status_led_pin = PIN_NC,
    .button_pin = PIN_NC,
    .pin_invert_mask = 0,
    .model_name = {.len = 9, .string = "CFG ERROR"},
};
static HwConfig hw_config;

static bool hw_config_validate(const HwConfig* config) {
    if((config->magic != HW_CONFIG_MAGIC) || (config->version != HW_CONFIG_VERSION)) return false;

    uint32_t crc_check = xcrc32((const unsigned char*)config, sizeof(HwConfig) - sizeof(uint32_t), 0xffffffff);
    if(crc_check != config->crc) return false;

    if(config->model_name.len >= ZCL_BASIC_MAX_LENGTH) return false;
    if(config->model_name.len != strlen(config->model_name.string)) return false;

    if(config->cur_rgb_code > HW_CUR_CODE_LIMIT_RGB || config->cur_cw_code > HW_CUR_CODE_LIMIT_CW) return false;
    if(config->cold_temp_k <= config->warm_temp_k || config->warm_temp_k == 0) return false;

    uint8_t seen = 0;
    for(uint8_t i = 0; i < LIGHT_CH_COUNT; i++) {
        if(config->out_map[i] >= LIGHT_CH_COUNT || (seen & (1 << config->out_map[i]))) return false;
        seen |= 1 << config->out_map[i];
    }

    return true;
}

bool hw_config_init(void) {
    flash_read(HW_CONFIG_ADDR, sizeof(HwConfig), (uint8_t*)&hw_config);
    if(hw_config_validate(&hw_config)) {
        return true;
    }

    do {
        if(hw_config_set == NULL) break;

        memcpy(&hw_config, hw_config_set, sizeof(HwConfig));
        hw_config.magic = HW_CONFIG_MAGIC;
        hw_config.version = HW_CONFIG_VERSION;
        hw_config.model_name.len = strlen(hw_config.model_name.string);
        hw_config.crc = xcrc32((const unsigned char*)&hw_config, sizeof(HwConfig) - sizeof(uint32_t), 0xffffffff);

        flash_erase(HW_CONFIG_PAGE);

        if(!flash_writeWithCheck(HW_CONFIG_ADDR, sizeof(HwConfig), (uint8_t*)&hw_config)) break;

        flash_read(HW_CONFIG_ADDR, sizeof(HwConfig), (uint8_t*)&hw_config);
        if(!hw_config_validate(&hw_config)) break;

        return true;
    } while(0);
    memcpy(&hw_config, &hw_config_default, sizeof(HwConfig));
    return false;
}

HwConfig* hw_config_get(void) {
    return &hw_config;
}
