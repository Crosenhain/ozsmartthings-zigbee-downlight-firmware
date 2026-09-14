// Zigbee2MQTT external converter for the custom DL41 RGBCW firmware.
// Install: copy to <z2m data>/external_converters/dl41_rgbcw.mjs (Converters, not Extensions).
//
// 1.0.05 (M1): on/off, brightness, colour temperature on the cool/warm LEDs.
// 1.0.06 (M2): + colour on the RGB LEDs via XY and hue/saturation (incl. enhanced hue).
//             Colour and colour temperature are separate modes, like stock.
// 1.0.09:     + effects. "colorloop" uses the ZCL ColorLoopSet command (full turn every `transition`
//             seconds, default 15), so "stop_colorloop" fades back to what was lit before the loop.
//             Scenes (scene_store / scene_add / scene_recall) now store and recall colour.
import * as m from "zigbee-herdsman-converters/lib/modernExtend";
import * as tz from "zigbee-herdsman-converters/converters/toZigbee";

// Listed before the extend's converters, so it wins for the "effect" key. Other effects go to the standard one.
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

export default {
    zigbeeModel: ["DL41-RGBCW", "DL41-CUSTOM-TEST"],
    model: "DL41-RGBCW",
    vendor: "Custom",
    description: "Oz Smart Things DL41 downlight on custom firmware (SM2235 RGBCW)",
    toZigbee: [effectWithColorLoop],
    extend: [
        m.light({
            // hw_variants.h: cold 6000 K (166 mireds), warm 3000 K (333 mireds).
            // startup: false because the firmware doesn't implement StartUpColorTemperatureMireds yet.
            colorTemp: {range: [166, 333], startup: false},
            color: {modes: ["xy", "hs"], enhancedHue: true},
            powerOnBehavior: true,
            // blink / breathe / okay / channel_change / finish_effect / stop_effect, plus colorloop / stop_colorloop
            effect: true,
        }),
    ],
    // OTA is a definition property in current zigbee-herdsman-converters (there is no m.ota() extend).
    ota: true,
};
