/**
 * @file    main.c
 * @brief   MonEEE - TM4C1294 firmware for 32-channel ADS1299 EEG acquisition
 * @version 6.0
 *
 * Application entry point, ISR handlers, and super-loop for the MonEEE
 * acquisition system.  All real-time data acquisition is interrupt-driven:
 *
 *   - ADSIntHandler (PP4, DRDY)  : Reads 4x ADS1299 via SSI2, fills eeeData[]
 *   - SSI0IntHandler (SSI0 TXFF) : Clocks eeeData[] out to the CM5 host
 *   - RPIIntHandler  (PA0, edge) : Receives 1-byte commands from CM5 via SSI0 RX
 *   - SysTickIntHandler          : 1 kHz tick for startup delay
 *
 * The main loop only handles impedance measurement, which is a blocking
 * sweep that cannot run inside an ISR.
 */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "inc/hw_memmap.h"
#include "inc/hw_ints.h"
#include "driverlib/gpio.h"
#include "driverlib/interrupt.h"
#include "driverlib/pin_map.h"
#include "driverlib/sysctl.h"
#include "driverlib/systick.h"
#include "driverlib/uart.h"
#include "utils/uartstdio.h"
#include "driverlib/ssi.h"

#include "Drivers/Board.h"
#include "Modules/ADS1299.h"

/* =========================================================================
 * CM5 SPI Command Bytes
 *
 * The CM5 host writes one command byte into the SSI0 TX FIFO, then asserts
 * a falling edge on PA0 to trigger RPIIntHandler.  The TM4C drains the
 * SSI0 RX FIFO and dispatches the command.
 * ========================================================================= */
#define CM5_CMD_START_EEG       0x01   /* Start continuous EEG data stream        */
#define CM5_CMD_STOP            0x02   /* Stop any active acquisition mode        */
#define CM5_CMD_START_IMPEDANCE 0x03   /* Start one-shot impedance measurement    */
#define CM5_CMD_START_TEST      0x04   /* Start shorted-input noise test          */

/* Startup delay: SysTick ticks to wait before peripheral init (~10 s) */
#define CLKCTRL_TIMEOUT  10000U

/* =========================================================================
 * Global variables - shared with ISR context (volatile required)
 *
 * eeeData[]       : 112-byte frame buffer filled by ADSIntHandler, read by
 *                   SSI0IntHandler.  Declared [140] for legacy headroom.
 * usb_buffer[]    : Per-ADS USB event bytes injected by USB CDC RxHandler,
 *                   consumed by ADSIntHandler when building frame headers.
 * usbCont         : Rolling index into usb_buffer[] (2..5), written by
 *                   RxHandler and reset by ADSIntHandler.
 * statusRPI       : True while an acquisition mode is active.  Guards
 *                   against redundant start/stop transitions.
 * flagIMP         : Set by RPIIntHandler when impedance mode is requested;
 *                   polled and cleared by the main loop.
 * g_ui32Seconds   : Millisecond counter incremented by SysTickIntHandler,
 *                   used only for startup delay.
 * g_cm5Command    : Last command byte received from CM5 via SSI0 RX FIFO.
 * Electrode_Impedance_Ohms : 32x32 sample matrix used exclusively by the
 *                   impedance sweep (main-loop context, not ISR).
 * ========================================================================= */
static volatile uint32_t g_ui32Seconds = 0;
volatile uint8_t eeeData[140];
volatile uint8_t usb_buffer[10];
volatile uint8_t usbCont = 2;
volatile bool statusRPI = false;
volatile bool flagIMP = false;
int32_t Electrode_Impedance_Ohms[32][32];

static volatile uint8_t g_cm5Command = 0x00;

/* =========================================================================
 * main - application entry point
 *
 * 1. Configure system clock to 120 MHz (PLL from 25 MHz XTAL).
 * 2. Wait ~10 s (CLKCTRL_TIMEOUT x 1 ms SysTick) for power stabilization.
 * 3. Initialize peripherals: GPIO, UART debug, ADS1299 (via boardReset),
 *    USB CDC, and SSI0 slave (CM5 interface).
 * 4. Enable DRDY interrupt (PP4 -> ADSIntHandler) and CM5 command
 *    interrupt (PA0 -> RPIIntHandler).
 * 5. Enter super-loop: only impedance sweep runs here (blocking).
 * ========================================================================= */
int main(void)
{
    /* 120 MHz from 25 MHz crystal + PLL (VCO = 240 MHz) */
    SysCtlClockFreqSet((SYSCTL_XTAL_25MHZ | SYSCTL_OSC_MAIN |
                         SYSCTL_USE_PLL   | SYSCTL_CFG_VCO_240), SYS_CLOCK_HZ);

    initSysTick();
    IntMasterEnable();

    /* Blocking wait for clock stabilization (~10 s at 1 kHz SysTick) */
    while (g_ui32Seconds < CLKCTRL_TIMEOUT);
    g_ui32Seconds = 0;

    /* Peripheral initialization sequence */
    initGeneral();      /* Enable GPIO port clocks                          */
    initGPIO();         /* Configure DRDY, RESET, CS, PWDN, PA0/PA1 pins   */
    initDebug();        /* UART2 @ 115200 for debug console                 */
    boardReset();       /* SSI2 master init + full ADS1299 power-up         */
    initUSB();          /* USB CDC device for event byte injection          */
    initRPI();          /* SSI0 slave + TX FIFO interrupt for CM5 link      */

    /* Enable NVIC interrupts for ADS DRDY (PP4) and CM5 command (PA0) */
    IntEnable(INT_GPIOP4);
    IntEnable(INT_GPIOA);

    UARTprintf("MonEEE Start\r\n");

    /* -- Super-loop --
     * EEG and Test modes are fully interrupt-driven (ADSIntHandler +
     * SSI0IntHandler).  Only the impedance sweep requires main-loop
     * execution because measureAllImpedances() is a long blocking
     * sequence that cannot safely run inside an ISR. */
    while (1)
    {
        if (flagIMP) {
            measureAllImpedances();     /* Blocking: sweeps 32 ch x 42 samples */
            calculateSendPkPk();        /* Packs Pk-Pk into eeeData, signals CM5 */
            flagIMP = false;
            g_cm5Command = 0x00;        /* Return to idle state */
        }
    }
}

/* =========================================================================
 * RPIIntHandler - CM5 command reception ISR (PA0 falling edge)
 *
 * Triggered when the CM5 asserts PA0 LOW after writing a command byte
 * into SSI0.  Drains the RX FIFO (keeps last byte), then dispatches
 * the command to start/stop the appropriate acquisition mode.
 *
 * NOTE: The guard (g_cm5Command != ACQ_MODE_IDLE) is currently
 * tautological because g_cm5Command was just assigned a value (0x01-0x04)
 * that can never equal ACQ_MODE_IDLE (0x05).  The effective guard is
 * statusRPI alone.  This is intentional safety - if ACQ_MODE_IDLE is
 * ever reassigned to 0x00, the guard becomes meaningful.
 * ========================================================================= */
void RPIIntHandler(void)
{
    uint32_t ui32RxData = 0;

    if (GPIOIntStatus(GPIO_PORTA_BASE, true) & GPIO_PIN_0)
    {
        /* Drain SSI0 RX FIFO - keep only the most recent command byte */
        while (SSIDataGetNonBlocking(SSI0_BASE, &ui32RxData))
        {
            g_cm5Command = (uint8_t)(ui32RxData & 0xFFU);
        }

        switch (g_cm5Command)
        {
            /* -- EEG: continuous 250 SPS, Gain 24, normal input ----------- */
            case CM5_CMD_START_EEG:
                if ((g_cm5Command != ACQ_MODE_IDLE) && !statusRPI)
                {
                    stopAcquisition();
                    statusRPI = true;
                }
                startEEGMode();
                break;

            /* -- STOP: halt any running mode ------------------------------ */
            case CM5_CMD_STOP:
                if ((g_cm5Command != ACQ_MODE_IDLE) && statusRPI)
                {
                    stopAcquisition();
                    statusRPI = false;
                    UARTprintf("MonEEE: Stream STOP\r\n");
                }
                break;

            /* -- IMPEDANCE: one-shot lead-off sweep (Gain 1, 6 uA AC) ---- */
            case CM5_CMD_START_IMPEDANCE:
                if ((g_cm5Command != ACQ_MODE_IDLE) && !statusRPI)
                {
                    stopAcquisition();
                    statusRPI = true;
                }
                startImpedanceMode();
                flagIMP = true;         /* Signal main loop to run sweep */
                break;

            /* -- TEST: shorted input noise measurement (Gain 24) ---------- */
            case CM5_CMD_START_TEST:
                if ((g_cm5Command != ACQ_MODE_IDLE) && !statusRPI)
                {
                    stopAcquisition();
                    statusRPI = true;
                }
                startTestShorted();
                break;

            /* -- Unknown command: safety stop ----------------------------- */
            default:
                if ((g_cm5Command != ACQ_MODE_IDLE) && statusRPI)
                {
                    stopAcquisition();
                    statusRPI = false;
                    UARTprintf("MonEEE: Stream STOP\r\n");
                }
                break;
        }
    }
    GPIOIntClear(GPIO_PORTA_BASE, GPIO_PIN_0);
}

/* =========================================================================
 * SysTickIntHandler - 1 kHz system tick
 *
 * Increments g_ui32Seconds used only during the startup delay in main().
 * Period is set by initSysTick() to (SYS_CLOCK_HZ / 1000) - 1.
 * ========================================================================= */
void SysTickIntHandler(void)
{
    g_ui32Seconds++;
}
