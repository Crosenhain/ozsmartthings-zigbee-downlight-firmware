// Zigbee2MQTT external converter for the custom DL41 RGBCW firmware.
// Install: copy to <z2m data>/external_converters/dl41_rgbcw.mjs (Converters, not Extensions).
//
// 1.0.05 (M1): on/off, brightness, colour temperature on the cool/warm LEDs.
// 1.0.06 (M2): + colour on the RGB LEDs via XY and hue/saturation (incl. enhanced hue).
//             Colour and colour temperature are separate modes, like stock.
// 1.0.09:     + effects. "colorloop" uses the ZCL ColorLoopSet command (full turn every `transition`
//             seconds, default 15), so "stop_colorloop" fades back to what was lit before the loop.
//             Scenes (scene_store / scene_add / scene_recall) now store and recall colour.
// Converter only: `color_temp_kelvin` sets and reports colour temperature in Kelvin (the device works in mireds).
// Converter only: binding and attribute reporting are set up on Configure, so the light reports its own state.
// Converter only: lights on firmware 1.0.14 or earlier get an On after every turn-on that carries a brightness or a
//             transition; they stayed dark otherwise (fixed in firmware 1.0.15).
import * as exposes from "zigbee-herdsman-converters/lib/exposes";
import * as m from "zigbee-herdsman-converters/lib/modernExtend";
import * as utils from "zigbee-herdsman-converters/lib/utils";
import * as tz from "zigbee-herdsman-converters/converters/toZigbee";

// hw_variants.h: cold 6000 K (166 mireds), warm 3000 K (333 mireds).
const COLOR_TEMP_RANGE_MIREDS = [166, 333];
const KELVIN_MIN = 3000;
const KELVIN_MAX = 6000;

const miredsToKelvin = (mireds) => Math.round(1000000 / mireds);

// Listed before the extend's converters, so these win for their keys.
const effectWithColorLoop = {
    key: ["effect"],
    convertSet: async (entity, key, value, meta) => {
        const effect = typeof value === "string" ? value.toLowerCase() : value;
        if (effect === "colorloop") {
            const seconds = Math.min(65535, Math.max(1, Math.round(Number(meta.message.transition ?? 15))));
            // updateflags: action | direction | time. action 2 = start from the current hue, direction 1 = increment.
            await entity.command("lightingColorCtrl", "colorLoopSet", {updateflags: 0x07, action: 2, direction: 1, time: seconds, starthue: 0});
            return;
        }
        if (effect === "stop_colorloop") {
            await entity.command("lightingColorCtrl", "colorLoopSet", {updateflags: 0x01, action: 0, direction: 0, time: 0, starthue: 0});
            return;
        }
        return tz.effect.convertSet(entity, key, value, meta);
    },
};

// Zigbee2MQTT sends a turn-on that carries a brightness or a transition as a lone MoveToLevelWithOnOff. Firmware 1.0.14
// and earlier only turned on from it when the level rose, and Off keeps the level, so {"state":"ON","brightness":N}
// (Home Assistant's light.toggle / light.turn_on with a brightness) usually left the light dark. Following it with On
// lights them; firmware with the fix ignores the extra command. Same keys as light_onoff_brightness, so it replaces it.
const FIRST_BUILD_TURNING_ON_FROM_LEVEL = 15;
const needsExplicitOn = (entity) => {
    // softwareBuildID is "v1.0.NN". Groups have none, and a member may still run an old build: send the On.
    const build = /^v1\.0\.(\d+)/.exec(entity.getDevice?.()?.softwareBuildID ?? "");
    return !build || Number(build[1]) < FIRST_BUILD_TURNING_ON_FROM_LEVEL;
};
const onOffBrightnessWithExplicitOn = {
    ...tz.light_onoff_brightness,
    convertSet: async (entity, key, value, meta) => {
        const result = await tz.light_onoff_brightness.convertSet(entity, key, value, meta);
        if (result?.state?.state === "ON" && result.state.brightness !== undefined && needsExplicitOn(entity)) {
            await entity.command("genOnOff", "on", {}, utils.getOptions(meta.mapped, entity));
        }
        return result;
    },
};

// Accepts Kelvin as well as the standard mireds/percent/preset keys, and keeps color_temp_kelvin in the
// published state whichever key was used. The device only ever receives MoveToColorTemperature in mireds.
const colorTempWithKelvin = {
    key: ["color_temp_kelvin", "color_temp", "color_temp_percent"],
    convertSet: async (entity, key, value, meta) => {
        if (key === "color_temp_kelvin") {
            const kelvin = Number(value);
            if (!Number.isFinite(kelvin) || kelvin <= 0) {
                throw new Error(`color_temp_kelvin must be a positive number of Kelvin, got '${value}'`);
            }
            key = "color_temp";
            value = Math.round(1000000 / kelvin); // light_colortemp clamps to the device's mireds range
        }
        const result = await tz.light_colortemp.convertSet(entity, key, value, meta);
        const mireds = result?.state?.color_temp;
        if (typeof mireds === "number" && mireds > 0) {
            result.state.color_temp_kelvin = miredsToKelvin(mireds);
        }
        return result;
    },
    convertGet: async (entity, key, meta) => {
        await tz.light_colortemp.convertGet(entity, "color_temp", meta);
    },
};

const colorTempKelvinFromDevice = {
    cluster: "lightingColorCtrl",
    type: ["attributeReport", "readResponse"],
    convert: (model, msg, publish, options, meta) => {
        const mireds = msg.data.colorTemperature;
        if (typeof mireds === "number" && mireds > 0 && mireds < 0xffff) {
            return {color_temp_kelvin: miredsToKelvin(mireds)};
        }
    },
};

export default {
    zigbeeModel: ["DL41-RGBCW", "DL41-CUSTOM-TEST"],
    model: "DL41-RGBCW",
    vendor: "Custom",
    description: "Oz Smart Things DL41 downlight on custom firmware (SM2235 RGBCW)",
    toZigbee: [effectWithColorLoop, colorTempWithKelvin, onOffBrightnessWithExplicitOn],
    fromZigbee: [colorTempKelvinFromDevice],
    exposes: [
        exposes.presets
            .numeric("color_temp_kelvin", exposes.access.ALL)
            .withUnit("K")
            .withValueMin(KELVIN_MIN)
            .withValueMax(KELVIN_MAX)
            .withValueStep(50)
            .withDescription("Colour temperature of the white LEDs in Kelvin (same setting as color_temp)"),
    ],
    extend: [
        m.light({
            // startup: false because the firmware doesn't implement StartUpColorTemperatureMireds yet.
            colorTemp: {range: COLOR_TEMP_RANGE_MIREDS, startup: false},
            color: {modes: ["xy", "hs"], enhancedHue: true},
            powerOnBehavior: true,
            // Bind and configure reporting on Configure, so the light reports its own state instead of
            // Zigbee2MQTT only assuming it (needed with the device's "optimistic" option turned off).
            configureReporting: true,
            // blink / breathe / okay / channel_change / finish_effect / stop_effect, plus colorloop / stop_colorloop
            effect: true,
        }),
    ],
    // OTA is a definition property in current zigbee-herdsman-converters (there is no m.ota() extend).
    ota: true,
};
