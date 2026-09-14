#include "color_math.h"

void color_hs_to_rgb(uint16_t hue, uint8_t sat, uint16_t* r, uint16_t* g, uint16_t* b) {
    uint32_t s = ((uint32_t)(sat > COLOR_SAT_MAX ? COLOR_SAT_MAX : sat) * 65535) / COLOR_SAT_MAX;
    uint32_t h6 = (uint32_t)hue * 6;
    uint32_t f = h6 & 0xFFFF;
    uint32_t p = 65535 - s;
    uint32_t q = 65535 - ((s * f) / 65535);
    uint32_t t = 65535 - ((s * (65535 - f)) / 65535);

    switch ((h6 >> 16) % 6) {
    case 0: *r = 65535; *g = (uint16_t)t; *b = (uint16_t)p; break;
    case 1: *r = (uint16_t)q; *g = 65535; *b = (uint16_t)p; break;
    case 2: *r = (uint16_t)p; *g = 65535; *b = (uint16_t)t; break;
    case 3: *r = (uint16_t)p; *g = (uint16_t)q; *b = 65535; break;
    case 4: *r = (uint16_t)t; *g = (uint16_t)p; *b = 65535; break;
    default: *r = 65535; *g = (uint16_t)p; *b = (uint16_t)q; break;
    }
}

void color_xy_to_rgb(uint16_t cx, uint16_t cy, uint16_t* r, uint16_t* g, uint16_t* b) {
    float x = (float)cx / 65536.0f;
    float y = (float)cy / 65536.0f;
    if (y < 0.0001f) y = 0.0001f;

    /* XYZ with Y = 1 */
    float X = x / y;
    float Z = (1.0f - x - y) / y;
    float fr = 3.2406f * X - 1.5372f - 0.4986f * Z;
    float fg = -0.9689f * X + 1.8758f + 0.0415f * Z;
    float fb = 0.0557f * X - 0.2040f + 1.0570f * Z;
    if (fr < 0) fr = 0;
    if (fg < 0) fg = 0;
    if (fb < 0) fb = 0;

    float m = fr > fg ? fr : fg;
    if (fb > m) m = fb;
    if (m <= 0.0f) {
        *r = *g = *b = 0;
        return;
    }
    *r = (uint16_t)(fr / m * 65535.0f + 0.5f);
    *g = (uint16_t)(fg / m * 65535.0f + 0.5f);
    *b = (uint16_t)(fb / m * 65535.0f + 0.5f);
}

void color_rgb_to_xy(uint16_t r, uint16_t g, uint16_t b, uint16_t* cx, uint16_t* cy) {
    float fr = (float)r / 65535.0f;
    float fg = (float)g / 65535.0f;
    float fb = (float)b / 65535.0f;

    /* linear sRGB (D65) to XYZ */
    float X = 0.4124f * fr + 0.3576f * fg + 0.1805f * fb;
    float Y = 0.2126f * fr + 0.7152f * fg + 0.0722f * fb;
    float Z = 0.0193f * fr + 0.1192f * fg + 0.9505f * fb;
    float sum = X + Y + Z;

    if (sum <= 0.0f) {
        *cx = 0x5050; /* D65: x 0.3127, y 0.3290 */
        *cy = 0x5439;
        return;
    }
    float x = X / sum * 65536.0f + 0.5f;
    float y = Y / sum * 65536.0f + 0.5f;
    *cx = x > 65279.0f ? 65279 : (uint16_t)x;
    *cy = y > 65279.0f ? 65279 : (uint16_t)y;
}

void color_rgb_to_hs(uint16_t r, uint16_t g, uint16_t b, uint16_t* hue, uint8_t* sat) {
    uint32_t max = r > g ? r : g;
    if (b > max) max = b;
    uint32_t min = r < g ? r : g;
    if (b < min) min = b;
    uint32_t delta = max - min;

    if (max == 0 || delta == 0) {
        *hue = 0;
        *sat = 0;
        return;
    }
    *sat = (uint8_t)((delta * COLOR_SAT_MAX + max / 2) / max);

    /* hue in 1/6 sectors of 65536; |diff| * 10922 <= 715.8M fits in int32 (no 64-bit libgcc on target) */
    int32_t h;
    if (max == r) {
        h = (((int32_t)g - (int32_t)b) * 10922) / (int32_t)delta;
    } else if (max == g) {
        h = 21845 + (((int32_t)b - (int32_t)r) * 10922) / (int32_t)delta;
    } else {
        h = 43690 + (((int32_t)r - (int32_t)g) * 10922) / (int32_t)delta;
    }
    if (h < 0) h += 65536;
    *hue = (uint16_t)(h & 0xFFFF);
}

int32_t color_hue_delta(uint16_t from, uint16_t to, uint8_t direction) {
    int32_t up = (uint16_t)(to - from); /* 0..65535 travelling upwards */
    int32_t down = up ? (up - 65536) : 0;

    switch (direction) {
    case 1: /* longest */
        return (up >= 32768) ? up : down;
    case 2: /* up */
        return up;
    case 3: /* down */
        return down;
    default: /* shortest */
        return (up <= 32768) ? up : down;
    }
}

static uint32_t isqrt32(uint32_t v) {
    uint32_t root = 0;
    uint32_t bit = 1UL << 30;

    while (bit > v) bit >>= 2;
    while (bit) {
        if (v >= root + bit) {
            v -= root + bit;
            root = (root >> 1) + bit;
        } else {
            root >>= 1;
        }
        bit >>= 2;
    }
    return root;
}

uint16_t color_encode(uint16_t lin) {
    uint32_t e = isqrt32((uint32_t)lin * 65535UL); /* <= 65535^2, fits in uint32 */
    return e > 65535 ? 65535 : (uint16_t)e;
}

uint16_t color_decode(uint16_t enc) {
    return (uint16_t)(((uint32_t)enc * enc + 32767UL) / 65535UL);
}

void color_hs_to_linear(uint16_t hue, uint8_t sat, uint16_t* r, uint16_t* g, uint16_t* b) {
    color_hs_to_rgb(hue, sat, r, g, b);
    *r = color_decode(*r);
    *g = color_decode(*g);
    *b = color_decode(*b);
}

uint16_t color_xy_to_blackbody_mireds(uint16_t cx, uint16_t cy, uint16_t max_dist) {
    float x = (float)cx / 65536.0f;
    float y = (float)cy / 65536.0f;

    if (y < 0.2f) return 0; /* nowhere near white; also keeps McCamy's denominator away from zero */

    /* McCamy's approximation (same constants Zigbee2MQTT uses for xy -> mireds) */
    float n = (x - 0.3320f) / (0.1858f - y);
    float t = ((437.0f * n + 3601.0f) * n + 6861.0f) * n + 5517.0f;
    if (t < 1667.0f || t > 25000.0f) return 0;

    /* Planckian locus at t (Kim et al. cubic spline) */
    float i1 = 1000.0f / t, i2 = i1 * i1, i3 = i2 * i1;
    float lx = (t <= 4000.0f) ? (-0.2661239f * i3 - 0.2343589f * i2 + 0.8776956f * i1 + 0.179910f)
                              : (-3.0258469f * i3 + 2.1070379f * i2 + 0.2226347f * i1 + 0.240390f);
    float lx2 = lx * lx, lx3 = lx2 * lx;
    float ly;
    if (t <= 2222.0f) {
        ly = -1.1063814f * lx3 - 1.34811020f * lx2 + 2.18555832f * lx - 0.20219683f;
    } else if (t <= 4000.0f) {
        ly = -0.9549476f * lx3 - 1.37418593f * lx2 + 2.09137015f * lx - 0.16748867f;
    } else {
        ly = 3.0817580f * lx3 - 5.87338670f * lx2 + 3.75112997f * lx - 0.37001483f;
    }

    float dx = x - lx, dy = y - ly;
    float lim = (float)max_dist / 65536.0f;
    if (dx * dx + dy * dy > lim * lim) return 0;
    return (uint16_t)(1000000.0f / t + 0.5f);
}

void color_blend(const uint16_t from[3], const uint16_t to[3], uint16_t alpha, uint16_t out[3]) {
    uint32_t lin[3];
    uint32_t max = 0;

    if (alpha > 1024) alpha = 1024;
    for (int i = 0; i < 3; i++) {
        uint32_t e = ((uint32_t)color_encode(from[i]) * (1024U - alpha) + (uint32_t)color_encode(to[i]) * alpha) >> 10;
        lin[i] = color_decode((uint16_t)e);
        if (lin[i] > max) max = lin[i];
    }
    for (int i = 0; i < 3; i++) {
        /* lin * 65535 <= 65535^2 fits in uint32 */
        out[i] = max ? (uint16_t)((lin[i] * 65535UL + max / 2) / max) : 0;
    }
}
