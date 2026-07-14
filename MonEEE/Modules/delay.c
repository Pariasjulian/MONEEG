/**
 * @file    delay.c
 * @brief   Busy-wait delay functions using SysCtlDelay (3 cycles/loop)
 * @author  ber-a
 * @date    Feb 1, 2018
 */

#include <stdint.h>
#include <stdbool.h>
#include "driverlib/sysctl.h"
#include "delay.h"

void delay_ms(uint32_t ui32ms) {
	SysCtlDelay(ui32ms * (sysClk / 3 / 1000));
}

void delay_us(uint32_t ui32us) {
	SysCtlDelay(ui32us * (sysClk / 3 / 1000000));
}

uint8_t constrain(uint8_t x, uint8_t minVal, uint8_t maxVal) {
    if (x < minVal)
        return minVal;
    else if (x > maxVal)
        return maxVal;
    else
        return x;
}
