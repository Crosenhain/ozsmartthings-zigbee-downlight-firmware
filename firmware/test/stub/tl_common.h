/* Host-side stub of the Telink SDK surface used by sm2235.c. */
#pragma once
#include <stdint.h>
#include <string.h>

typedef enum { AS_GPIO = 0 } GPIO_FuncTypeDef;
typedef uint32_t u32;
typedef int bool;
#define true 1
#define false 0

void gpio_write(uint32_t pin, unsigned value);
void gpio_set_output_en(uint32_t pin, unsigned value);
void gpio_set_input_en(uint32_t pin, unsigned value);
void gpio_set_func(uint32_t pin, GPIO_FuncTypeDef func);
void sleep_us(unsigned long us);
u32 drv_disable_irq(void);
u32 drv_restore_irq(u32 en);
