/**
 * @file    delay.h
 * @brief   Busy-wait delay functions and value clamping utility
 * @author  ber-a
 * @date    Feb 1, 2018
 */

#ifndef DELAY_H_
#define DELAY_H_

/* System clock frequency used by SysCtlDelay() timing calculations */
#define sysClk 120000000

void delay_ms(uint32_t ui32ms);
void delay_us(uint32_t ui32us);
uint8_t constrain(uint8_t x, uint8_t minVal, uint8_t maxVal);

#endif /* DELAY_H_ */
