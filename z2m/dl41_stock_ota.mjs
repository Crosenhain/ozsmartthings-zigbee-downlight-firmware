// Zigbee2MQTT external converter for Oz Smart Things DL41 lights still on the STOCK Tuya firmware.
// Install next to dl41_rgbcw.mjs in <z2m data>/external_converters/ (Converters, not Extensions).
//
// Identical to Zigbee2MQTT's built-in DL41-03-10-R-ZB definition, but with `ota: true`, so Zigbee2MQTT
// checks the override OTA index for these lights and can show the conversion image as an available update.
// Limited to _TZ3210_klsm24op, the variant the conversion was proven on. Once a light is converted it
// reports model DL41-RGBCW and dl41_rgbcw.mjs takes over. Remove this file when no stock lights remain.
import * as tuya from "zigbee-herdsman-converters/lib/tuya";

export default {
    fingerprint: tuya.fingerprint("TS0505B", ["_TZ3210_klsm24op"]),
    model: "DL41-03-10-R-ZB",
    vendor: "Oz Smart Things",
    description: "Oz Smart RGBW Zigbee downlight 10w (stock firmware, OTA conversion enabled)",
    extend: [tuya.modernExtend.tuyaLight({colorTemp: {range: [153, 500]}, color: true})],
    ota: true,
};
