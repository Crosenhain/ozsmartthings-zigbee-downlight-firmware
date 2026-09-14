/* Host test for color_math.c.
 * Build: gcc -std=gnu99 -Wall -I../src test_color_math.c ../src/color_math.c -o test_color_math */
#include <stdio.h>
#include <stdlib.h>
#include "color_math.h"

static int errors = 0;

static int near(int a, int b, int tol) { return abs(a - b) <= tol; }

static void check_rgb(const char *what, uint16_t r, uint16_t g, uint16_t b, int er, int eg, int eb, int tol) {
    int ok = near(r, er, tol) && near(g, eg, tol) && near(b, eb, tol);
    printf("%s %-34s -> %5u %5u %5u  (expect %5d %5d %5d ±%d)\n", ok ? "ok  " : "FAIL", what, r, g, b, er, eg, eb, tol);
    if (!ok) errors++;
}

static void check_delta(const char *what, int32_t got, int32_t exp) {
    int ok = got == exp;
    printf("%s %-34s -> %6d (expect %6d)\n", ok ? "ok  " : "FAIL", what, got, exp);
    if (!ok) errors++;
}

static uint16_t xy(double v) { return (uint16_t)(v * 65536.0 + 0.5); }

int main(void) {
    uint16_t r, g, b;

    /* Hue/saturation */
    color_hs_to_rgb(0, 254, &r, &g, &b);     check_rgb("HS red (0 deg)", r, g, b, 65535, 0, 0, 2);
    color_hs_to_rgb(10922, 254, &r, &g, &b); check_rgb("HS yellow (60 deg)", r, g, b, 65535, 65535, 0, 30);
    color_hs_to_rgb(21845, 254, &r, &g, &b); check_rgb("HS green (120 deg)", r, g, b, 0, 65535, 0, 30);
    color_hs_to_rgb(32768, 254, &r, &g, &b); check_rgb("HS cyan (180 deg)", r, g, b, 0, 65535, 65535, 30);
    color_hs_to_rgb(43690, 254, &r, &g, &b); check_rgb("HS blue (240 deg)", r, g, b, 0, 0, 65535, 30);
    color_hs_to_rgb(54613, 254, &r, &g, &b); check_rgb("HS magenta (300 deg)", r, g, b, 65535, 0, 65535, 30);
    color_hs_to_rgb(12345, 0, &r, &g, &b);   check_rgb("HS sat 0 = white", r, g, b, 65535, 65535, 65535, 2);
    color_hs_to_rgb(0, 127, &r, &g, &b);     check_rgb("HS half-sat red (pink)", r, g, b, 65535, 32767, 32767, 300);
    color_hs_to_rgb(65535, 254, &r, &g, &b); check_rgb("HS hue 65535 wraps to red", r, g, b, 65535, 0, 6, 30);

    /* CIE xy -> linear sRGB, normalised */
    color_xy_to_rgb(xy(0.3127), xy(0.3290), &r, &g, &b); check_rgb("XY D65 white", r, g, b, 65535, 65535, 65535, 700);
    color_xy_to_rgb(xy(0.64), xy(0.33), &r, &g, &b);     check_rgb("XY sRGB red primary", r, g, b, 65535, 0, 0, 700);
    color_xy_to_rgb(xy(0.30), xy(0.60), &r, &g, &b);     check_rgb("XY sRGB green primary", r, g, b, 0, 65535, 0, 700);
    color_xy_to_rgb(xy(0.15), xy(0.06), &r, &g, &b);     check_rgb("XY sRGB blue primary", r, g, b, 0, 0, 65535, 700);
    color_xy_to_rgb(xy(0.70), xy(0.29), &r, &g, &b);     check_rgb("XY outside gamut red clamps", r, g, b, 65535, 0, 0, 1500);
    color_xy_to_rgb(0, 0, &r, &g, &b);
    printf("%s XY (0,0) doesn't crash, gives %u %u %u\n", "ok  ", r, g, b);

    /* Round trips used when switching between HS and XY modes */
    static const struct { uint16_t hue; uint8_t sat; const char *name; } hs_cases[] = {
        {0, 254, "red"}, {10922, 254, "yellow"}, {21845, 254, "green"}, {32768, 200, "cyan-ish"},
        {43690, 254, "blue"}, {54613, 120, "pastel magenta"}, {5000, 60, "pale orange"},
    };
    for (unsigned i = 0; i < sizeof(hs_cases) / sizeof(hs_cases[0]); i++) {
        uint16_t h2; uint8_t s2;
        color_hs_to_rgb(hs_cases[i].hue, hs_cases[i].sat, &r, &g, &b);
        color_rgb_to_hs(r, g, b, &h2, &s2);
        int dh = (int)(uint16_t)(h2 - hs_cases[i].hue); if (dh > 32768) dh = 65536 - dh;
        int ok = dh <= 60 && abs((int)s2 - hs_cases[i].sat) <= 1;
        printf("%s HS->RGB->HS %-18s hue %5u->%5u sat %3u->%3u\n", ok ? "ok  " : "FAIL", hs_cases[i].name,
               hs_cases[i].hue, h2, hs_cases[i].sat, s2);
        if (!ok) errors++;

        uint16_t x, y, r2, g2, b2;
        color_rgb_to_xy(r, g, b, &x, &y);
        color_xy_to_rgb(x, y, &r2, &g2, &b2);
        ok = near(r2, r, 900) && near(g2, g, 900) && near(b2, b, 900);
        printf("%s RGB->XY->RGB %-17s %5u %5u %5u -> %5u %5u %5u\n", ok ? "ok  " : "FAIL", hs_cases[i].name,
               r, g, b, r2, g2, b2);
        if (!ok) errors++;
    }
    color_rgb_to_xy(0, 0, 0, &r, &g);
    printf("%s RGB black -> D65 xy fallback %u %u\n", (r == 0x5050 && g == 0x5439) ? "ok  " : "FAIL", r, g);

    /* Perceptual encode/decode */
    {
        int worst = 0;
        for (uint32_t v = 0; v <= 65535; v += 7) {
            int d = abs((int)color_decode(color_encode((uint16_t)v)) - (int)v);
            if (d > worst) worst = d;
        }
        int ok = worst <= 260 && color_encode(0) == 0 && color_encode(65535) == 65535 && color_decode(65535) == 65535;
        printf("%s encode/decode round trip, worst error %d, ends 0/65535 exact\n", ok ? "ok  " : "FAIL", worst);
        if (!ok) errors++;
        ok = color_decode(32768) >= 16380 && color_decode(32768) <= 16390;
        printf("%s decode(half) = %u (quarter light)\n", ok ? "ok  " : "FAIL", color_decode(32768));
        if (!ok) errors++;
    }

    /* HS rendering: primaries/secondaries unchanged, mixed hues darker in between */
    color_hs_to_linear(0, 254, &r, &g, &b);     check_rgb("HS linear red", r, g, b, 65535, 0, 0, 2);
    color_hs_to_linear(54613, 254, &r, &g, &b); check_rgb("HS linear magenta", r, g, b, 65535, 0, 65535, 30);
    color_hs_to_linear(5461, 254, &r, &g, &b);  check_rgb("HS linear orange (30 deg)", r, g, b, 65535, 16384, 0, 60);

    /* Perceptual blends */
    {
        static const uint16_t red[3] = {65535, 0, 0}, blue[3] = {0, 0, 65535}, green[3] = {0, 65535, 0};
        uint16_t o[3];
        color_blend(red, blue, 0, o);    check_rgb("blend red->blue alpha 0", o[0], o[1], o[2], 65535, 0, 0, 0);
        color_blend(red, blue, 1024, o); check_rgb("blend red->blue alpha 1024", o[0], o[1], o[2], 0, 0, 65535, 0);
        color_blend(red, blue, 512, o);  check_rgb("blend red->blue mid = magenta", o[0], o[1], o[2], 65535, 0, 65535, 2);
        color_blend(red, blue, 256, o);  check_rgb("blend red->blue quarter", o[0], o[1], o[2], 65535, 0, 7282, 60);
        color_blend(blue, red, 256, o);  check_rgb("blend blue->red quarter", o[0], o[1], o[2], 7282, 0, 65535, 60);
        color_blend(red, green, 512, o); check_rgb("blend red->green mid = yellow", o[0], o[1], o[2], 65535, 65535, 0, 2);

        /* evenness: encoded blue share must rise monotonically and pass 50% at the midpoint */
        int mono = 1, prev = -1;
        for (uint16_t a = 0; a <= 1024; a += 16) {
            color_blend(red, blue, a, o);
            int share = (int)color_encode(o[2]) * 1000 / ((int)color_encode(o[0]) + (int)color_encode(o[2]));
            if (share < prev) mono = 0;
            prev = share;
        }
        printf("%s blend red->blue encoded share monotonic\n", mono ? "ok  " : "FAIL");
        if (!mono) errors++;

        /* the old linear-xy path, for comparison: how far (in encoded share) it is at a quarter of the way */
        uint16_t xr = xy(0.7006), yr = xy(0.2993), xb = xy(0.1355), yb = xy(0.0399);
        color_xy_to_rgb((uint16_t)(xr + (xb - xr) / 4), (uint16_t)(yr + (yb - yr) / 4), &r, &g, &b);
        int lin_share = (int)color_encode(b) * 1000 / ((int)color_encode(r) + (int)color_encode(b));
        color_blend(red, blue, 256, o);
        int new_share = (int)color_encode(o[2]) * 1000 / ((int)color_encode(o[0]) + (int)color_encode(o[2]));
        printf("info red->blue at 25%%: old xy path blue share %d/1000, new blend %d/1000\n", lin_share, new_share);
    }

    /* Black-body detection for colour-temperature scenes (xy values from Zigbee2MQTT's kelvinToXy table) */
    {
        static const struct { double x, y; int expect_mireds; const char *name; } bb[] = {
            {0.436929833678155, 0.404073616886221, 333, "Z2M 3000 K"},
            {0.459857791746717, 0.410598847355503, 370, "Z2M 2700 K"},
            {0.380438429420364, 0.376746069841299, 250, "Z2M 4000 K"},
            {0.322082269887888, 0.331752126277376, 167, "Z2M 6000 K"},
            {0.313524949311538, 0.323626953980771, 154, "Z2M 6500 K"},
            {0.3127, 0.3290, 154, "D65 white (just off the line)"},
            {0.7006, 0.2993, 0, "red"},
            {0.1355, 0.0399, 0, "blue"},
            {0.1724, 0.7468, 0, "green"},
            {0.35, 0.32, 0, "pale pink"},
            {0.40, 0.45, 0, "pale yellow-green"},
            {0.5267, 0.4133, 500, "Z2M 2000 K"},
        };
        for (unsigned i = 0; i < sizeof(bb) / sizeof(bb[0]); i++) {
            int got = color_xy_to_blackbody_mireds(xy(bb[i].x), xy(bb[i].y), 656 /* 0.01 in xy */);
            int ok = bb[i].expect_mireds ? near(got, bb[i].expect_mireds, 4) : got == 0;
            printf("%s blackbody %-30s -> %3d mireds (expect %s%d)\n", ok ? "ok  " : "FAIL", bb[i].name, got,
                   bb[i].expect_mireds ? "~" : "", bb[i].expect_mireds);
            if (!ok) errors++;
        }
    }

    /* Hue direction */
    check_delta("shortest 0 -> 1000", color_hue_delta(0, 1000, 0), 1000);
    check_delta("shortest 0 -> 65000 (wraps down)", color_hue_delta(0, 65000, 0), -536);
    check_delta("longest 0 -> 1000", color_hue_delta(0, 1000, 1), -64536);
    check_delta("up 65000 -> 1000 (wraps)", color_hue_delta(65000, 1000, 2), 1536);
    check_delta("down 1000 -> 65000 (wraps)", color_hue_delta(1000, 65000, 3), -1536);
    check_delta("same hue", color_hue_delta(4000, 4000, 0), 0);

    printf(errors ? "\n%d FAILURE(S)\n" : "\nALL PASSED\n", errors);
    return errors ? 1 : 0;
}
