/**
 * @file    Board.h
 * @brief   TM4C1294 board-level peripheral init declarations and constants
 * @author  ber-a
 * @date    Sep 8, 2025
 */

#ifndef DRIVERS_BOARD_H_
#define DRIVERS_BOARD_H_

/* System clock: 120 MHz from 25 MHz XTAL + PLL (VCO 240 MHz) */
#define SYS_CLOCK_HZ           120000000
#define SYSTICKS_PER_SECOND    100       /* Unused (legacy)                */
#define SYSTICKS_PER_MSECOND   1000      /* SysTick period: 1 kHz = 1 ms   */

extern volatile uint8_t usb_buffer[10];
extern volatile uint8_t usbCont;
/*!
 *  @brief  Initialize the general board specific settings
 *
 *  This function initializes the general board specific settings.
 *  This includes:
 *     - Enable clock sources for peripherals
 */
extern void initGeneral(void);

/*!
 *  @brief  Initialize board specific GPIO settings
 *
 *  This function initializes the board specific GPIO settings.
 *
 */
extern void initGPIO(void);

/*!
 *  @brief  Initialize debug module settings
 *
 *  This function initializes the board specific UART2 settings and then calls
 *  the DEBUG_init API to initialize the debug module.
 *
 */
extern void initDebug(void);

/*!
 *  @brief  Initialize USB module settings
 *
 *  This function initializes the board specific USB settings and then calls
 *  the USB_init API to initialize the USB module.
 *
 */
extern void initUSB(void);

/*!
 *  @brief  Initialize SPI_A module settings
 *
 *  This function initializes the board specific SPI2 settings and then calls
 *  the SPIA_init API to initialize the SPI module.
 *
 */
extern void initSPI_A(void);

/*!
 *  @brief  Initialize RPI module settings
 *
 *  This function initializes the board specific SPI0 settings and then calls
 *  the RPI_init API to initialize the SPI module.
 *
 */
extern void initRPI(void);

/*!
 *  @brief  Initialize SysTick module settings
 *
 *  This function initializes the board specific SysTick settings and then calls
 *  the SysTick_init API to initialize the SysTick module.
 *
 */
void initSysTick(void);

#endif /* DRIVERS_BOARD_H_ */
