#include "tl_common.h"
#include "sm2235.h"

#define MODE_STANDBY   0xC0
#define MODE_RGB       0xC8
#define MODE_CW        0xD0
#define MODE_ALL       0xD8

#define CUR_CODE_MAX   15
#define HALF_PERIOD_US 4 /* ~40-60 kHz with GPIO overhead; datasheet allows 1-1000 kHz */

static uint32_t pin_scl = SM2235_PIN_NC;
static uint32_t pin_sda = SM2235_PIN_NC;
static uint8_t cur_rgb = 0;
static uint8_t cur_cw = 0;
static uint16_t last_out[SM2235_CHANNELS];
static bool have_last = false;

static inline void scl(unsigned v) {
    gpio_write(pin_scl, v);
}

static inline void sda(unsigned v) {
    gpio_write(pin_sda, v);
}

static void write_byte(uint8_t b) {
    /* Mask interrupts for one byte (~0.1 ms) so a radio IRQ can't stretch its clock pulses.
     * Between bytes the clock rests low, which the chip tolerates, and the radio gets serviced. */
    u32 irq = drv_disable_irq();
    for (uint8_t mask = 0x80; mask; mask >>= 1) {
        sda((b & mask) ? 1 : 0);
        sleep_us(HALF_PERIOD_US);
        scl(1);
        sleep_us(HALF_PERIOD_US);
        scl(0);
    }
    /* ACK clock: release DATA (chip has an internal pull-up), don't read it. */
    gpio_set_output_en(pin_sda, 0);
    sleep_us(HALF_PERIOD_US);
    scl(1);
    sleep_us(HALF_PERIOD_US);
    scl(0);
    gpio_set_output_en(pin_sda, 1);
    drv_restore_irq(irq);
    sleep_us(HALF_PERIOD_US);
}

static void send_frame(const uint8_t *frame, uint8_t len) {
    /* START: DATA falls while CLK is high (bus idles high). */
    sda(1);
    scl(1);
    sleep_us(HALF_PERIOD_US);
    sda(0);
    sleep_us(HALF_PERIOD_US);
    scl(0);
    sleep_us(HALF_PERIOD_US);

    for (uint8_t i = 0; i < len; i++) {
        write_byte(frame[i]);
    }

    /* STOP: DATA rises while CLK is high; leave both lines high (lowest standby current). */
    sda(0);
    sleep_us(HALF_PERIOD_US);
    scl(1);
    sleep_us(HALF_PERIOD_US);
    sda(1);
    sleep_us(HALF_PERIOD_US);
}

static void send_values(uint8_t mode, const uint16_t out[SM2235_CHANNELS]) {
    uint8_t frame[2 + 2 * SM2235_CHANNELS];

    frame[0] = mode;
    /* Unused group's current nibble is zero, as ESPHome sends it. */
    frame[1] = (mode == MODE_RGB) ? (uint8_t)(cur_rgb << 4) :
               (mode == MODE_CW)  ? cur_cw :
               (mode == MODE_ALL) ? (uint8_t)((cur_rgb << 4) | cur_cw) : 0;
    for (uint8_t i = 0; i < SM2235_CHANNELS; i++) {
        uint16_t v = out[i] > SM2235_MAX_VALUE ? SM2235_MAX_VALUE : out[i];
        frame[2 + 2 * i] = (uint8_t)(v >> 8);
        frame[3 + 2 * i] = (uint8_t)(v & 0xFF);
    }
    send_frame(frame, sizeof(frame));
}

static void enter_standby(void) {
    static const uint16_t zero[SM2235_CHANNELS] = {0};
    /* Clear all channels first, then sleep (ESPHome PR #5526 ordering). */
    send_values(MODE_ALL, zero);
    send_values(MODE_STANDBY, zero);
}

void sm2235_init(uint32_t scl_pin, uint32_t sda_pin, uint8_t cur_rgb_code, uint8_t cur_cw_code) {
    if (scl_pin == SM2235_PIN_NC || sda_pin == SM2235_PIN_NC || scl_pin == sda_pin) {
        pin_scl = pin_sda = SM2235_PIN_NC;
        return;
    }
    pin_scl = scl_pin;
    pin_sda = sda_pin;
    cur_rgb = cur_rgb_code > CUR_CODE_MAX ? CUR_CODE_MAX : cur_rgb_code;
    cur_cw = cur_cw_code > CUR_CODE_MAX ? CUR_CODE_MAX : cur_cw_code;

    for (uint8_t i = 0; i < 2; i++) {
        uint32_t pin = i ? pin_sda : pin_scl;
        gpio_set_func(pin, AS_GPIO);
        gpio_write(pin, 1);
        gpio_set_output_en(pin, 1);
        gpio_set_input_en(pin, 0);
    }

    enter_standby();
    memset(last_out, 0, sizeof(last_out));
    have_last = true;
}

/* Send out[] as a frame, or clear + standby if all zero. Returns non-zero if any channel is lit. */
static int send_state(const uint16_t out[SM2235_CHANNELS]) {
    int rgb = out[0] || out[1] || out[2];
    int cw = out[3] || out[4];

    if (!rgb && !cw) {
        enter_standby();
        return 0;
    }
    send_values(rgb && cw ? MODE_ALL : (rgb ? MODE_RGB : MODE_CW), out);
    return 1;
}

int sm2235_set(const uint16_t out[SM2235_CHANNELS]) {
    if (pin_scl == SM2235_PIN_NC) {
        return 0;
    }
    if (have_last && memcmp(last_out, out, sizeof(last_out)) == 0) {
        return 0;
    }

    send_state(out);
    memcpy(last_out, out, sizeof(last_out));
    have_last = true;
    return 1;
}

void sm2235_invalidate(void) {
    have_last = false;
}

int sm2235_refresh(void) {
    if (pin_scl == SM2235_PIN_NC || !have_last) {
        return 0;
    }
    return send_state(last_out);
}
