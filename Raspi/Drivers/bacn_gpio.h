#ifndef BACN_GPIO_H
#define BACN_GPIO_H

#include <stdbool.h>
#include <stdint.h>

#define START_PIN       4
#define READY_PIN       5

uint8_t status_DRDY(void);
uint8_t Start_INI(void);
uint8_t Start_ADQ(void);

// Added for main.c compatibility
int gpio_init(void);
void gpio_cleanup(void);

#endif // BACN_GPIO_H