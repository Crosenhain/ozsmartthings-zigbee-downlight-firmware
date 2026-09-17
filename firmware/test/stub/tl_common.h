/* Host-side stub of the Telink SDK surface used by sm2235.c and light_control.c. */
#pragma once
#include <stdint.h>
#include <string.h>

typedef enum { AS_GPIO = 0 } GPIO_FuncTypeDef;
typedef uint32_t u32;
typedef int bool;
#define true 1
#define false 0
#define _attribute_packed_ __attribute__((packed))

void gpio_write(uint32_t pin, unsigned value);
void gpio_set_output_en(uint32_t pin, unsigned value);
void gpio_set_input_en(uint32_t pin, unsigned value);
void gpio_set_func(uint32_t pin, GPIO_FuncTypeDef func);
void sleep_us(unsigned long us);
u32 drv_disable_irq(void);
u32 drv_restore_irq(u32 en);

/* Software timers (light_control.c): the test owns the clock. A callback returns < 0 to stop, 0 to keep its
 * interval, or a new interval in ms. */
typedef int (*ev_timer_callback_t)(void* arg);
typedef struct ev_timer_event_t {
    ev_timer_callback_t cb;
    void* arg;
    uint32_t interval;
    uint32_t due;
    uint32_t serial; /* changes when the slot is reused */
    int used;
} ev_timer_event_t;
ev_timer_event_t* sim_timer_schedule(ev_timer_callback_t cb, void* arg, uint32_t ms);
void sim_timer_cancel(ev_timer_event_t** evt);
#define TL_ZB_TIMER_SCHEDULE(cb, arg, ms) sim_timer_schedule((cb), (arg), (ms))
#define TL_ZB_TIMER_CANCEL(evt)           sim_timer_cancel(evt)
