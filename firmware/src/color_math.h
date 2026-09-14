#pragma once
#include <stdint.h>

/* Pure colour conversions (no SDK dependencies, host-testable). */

#define COLOR_SAT_MAX 254

/* HSV with V = full to RGB. hue: 0..65535 around the wheel, sat: 0..254. Outputs 0..65535. */
void color_hs_to_rgb(uint16_t hue, uint8_t sat, uint16_t* r, uint16_t* g, uint16_t* b);

/* CIE 1931 xy (value * 65536) to linear sRGB (D65), normalised so the brightest channel is 65535. */
void color_xy_to_rgb(uint16_t cx, uint16_t cy, uint16_t* r, uint16_t* g, uint16_t* b);

/* Linear sRGB (0..65535 each) to CIE xy (value * 65536). Black returns the D65 white point. */
void color_rgb_to_xy(uint16_t r, uint16_t g, uint16_t b, uint16_t* cx, uint16_t* cy);

/* RGB (0..65535 each) to hue (0..65535) and saturation (0..254). Grey returns hue 0, sat 0. */
void color_rgb_to_hs(uint16_t r, uint16_t g, uint16_t b, uint16_t* hue, uint8_t* sat);

/* Signed distance from 'from' to 'to' in enhanced hue units for a ZCL move-to-hue direction
 * (0 shortest, 1 longest, 2 up, 3 down). */
int32_t color_hue_delta(uint16_t from, uint16_t to, uint8_t direction);

/* Perceptual (gamma 2.0, matching the level curve) encoding of one 0..65535 channel.
 * LEDs are linear in light; the eye isn't, so fades run on encoded values. */
uint16_t color_encode(uint16_t lin);
uint16_t color_decode(uint16_t enc);

/* Hue/saturation to linear RGB for the LEDs: the HSV result is treated as encoded (as on a screen)
 * and decoded, so hue sweeps look even and mixed hues match what a colour picker shows. */
void color_hs_to_linear(uint16_t hue, uint8_t sat, uint16_t* r, uint16_t* g, uint16_t* b);

/* Blend two linear RGB colours (each normalised, brightest channel 65535) at alpha 0..1024 towards 'to'.
 * Interpolates encoded channels, decodes, then renormalises the brightest channel to 65535 so a fade
 * through a mix (e.g. red -> blue via magenta) keeps its brightness. */
void color_blend(const uint16_t from[3], const uint16_t to[3], uint16_t alpha, uint16_t out[3]);

/* If CIE xy (value * 65536) lies within max_dist (same units) of the black-body line, return its colour
 * temperature in mireds, otherwise 0. Zigbee2MQTT stores colour-temperature scenes as xy. */
uint16_t color_xy_to_blackbody_mireds(uint16_t cx, uint16_t cy, uint16_t max_dist);
