/**
 * @file    Board.c
 * @brief   TM4C1294 peripheral initialization and USB CDC callbacks
 * @author  ber-a
 * @date    Sep 8, 2025
 *
 * Configures GPIO, UART debug, SSI2 (ADS SPI master), SSI0 (CM5 SPI
 * slave), USB CDC device, and SysTick timer.  Also contains the USB
 * CDC control/TX/RX callback handlers.
 */

#include <stdbool.h>
#include <stdint.h>
//#include <string.h>
//#include <stdlib.h>
//#include <stdio.h>
#include "inc/hw_memmap.h"
#include "inc/hw_ints.h"
#include "driverlib/gpio.h"
#include "driverlib/interrupt.h"
#include "driverlib/pin_map.h"
#include "driverlib/sysctl.h"
#include "driverlib/ssi.h"
#include "driverlib/systick.h"
#include "driverlib/uart.h"
#include "driverlib/usb.h"
#include "usblib/usblib.h"
#include "usblib/usbcdc.h"
#include "usblib/usb-ids.h"
#include "usblib/device/usbdevice.h"
#include "usblib/device/usbdcdc.h"
#include "utils/ustdlib.h"
#include "usb_structs.h"

#include "utils/uartstdio.h"

#include "Board.h"

//****************************************************************************
//
// Global flag indicating that a USB configuration has been set.
//
//****************************************************************************
static volatile bool g_bUSBConfigured = false;
extern volatile uint8_t usbCont;

/*
 *  ======== initGeneral ========
 */
void initGeneral(void)
{
    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOA);//-
    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOB);//-
    //SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOC);//-
    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOD);//-
    //SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOE);//-
    //SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOF);//-
    //SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOG);//-
    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOJ);//-
    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOL);//-
    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPION);//-
    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOP);//-
    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOQ);//-
}

/*
 *  ======== initGPIO ========
 */
void initGPIO(void)
{
    /* Initialize peripheral and pins */

    //Input DRDY_A ADS1299
    GPIOPinTypeGPIOInput(GPIO_PORTP_BASE, GPIO_PIN_4);
    GPIOIntTypeSet(GPIO_PORTP_BASE, GPIO_PIN_4, GPIO_FALLING_EDGE);
    GPIOIntEnable(GPIO_PORTP_BASE, GPIO_INT_PIN_4);

    //Output RESET1 A ADS1299
    GPIOPadConfigSet(GPIO_PORTP_BASE, GPIO_PIN_3, GPIO_STRENGTH_2MA, GPIO_PIN_TYPE_STD);
    GPIOPinTypeGPIOOutput(GPIO_PORTP_BASE, GPIO_PIN_3);
    GPIOPinWrite(GPIO_PORTP_BASE, GPIO_PIN_3, GPIO_PIN_3);

    //Output RESET2 A ADS1299
    GPIOPadConfigSet(GPIO_PORTN_BASE, GPIO_PIN_0, GPIO_STRENGTH_2MA, GPIO_PIN_TYPE_STD);
    GPIOPinTypeGPIOOutput(GPIO_PORTN_BASE, GPIO_PIN_0);
    GPIOPinWrite(GPIO_PORTN_BASE, GPIO_PIN_0, GPIO_PIN_0);

    //Output RESET3 A ADS1299
    GPIOPadConfigSet(GPIO_PORTN_BASE, GPIO_PIN_3, GPIO_STRENGTH_2MA, GPIO_PIN_TYPE_STD);
    GPIOPinTypeGPIOOutput(GPIO_PORTN_BASE, GPIO_PIN_3);
    GPIOPinWrite(GPIO_PORTN_BASE, GPIO_PIN_3, GPIO_PIN_3);

    //Output RESET4 A ADS1299
    GPIOPadConfigSet(GPIO_PORTJ_BASE, GPIO_PIN_0, GPIO_STRENGTH_2MA, GPIO_PIN_TYPE_STD);
    GPIOPinTypeGPIOOutput(GPIO_PORTJ_BASE, GPIO_PIN_0);
    GPIOPinWrite(GPIO_PORTJ_BASE, GPIO_PIN_0, GPIO_PIN_0);

    //Output CS1 A ADS1299
    GPIOPadConfigSet(GPIO_PORTD_BASE, GPIO_PIN_2, GPIO_STRENGTH_2MA, GPIO_PIN_TYPE_STD);
    GPIOPinTypeGPIOOutput(GPIO_PORTD_BASE, GPIO_PIN_2);
    GPIOPinWrite(GPIO_PORTD_BASE, GPIO_PIN_2, GPIO_PIN_2);

    //Output CS2 A ADS1299
    GPIOPadConfigSet(GPIO_PORTN_BASE, GPIO_PIN_1, GPIO_STRENGTH_2MA, GPIO_PIN_TYPE_STD);
    GPIOPinTypeGPIOOutput(GPIO_PORTN_BASE, GPIO_PIN_1);
    GPIOPinWrite(GPIO_PORTN_BASE, GPIO_PIN_1, GPIO_PIN_1);

    //Output CS3 A ADS1299
    GPIOPadConfigSet(GPIO_PORTN_BASE, GPIO_PIN_4, GPIO_STRENGTH_2MA, GPIO_PIN_TYPE_STD);
    GPIOPinTypeGPIOOutput(GPIO_PORTN_BASE, GPIO_PIN_4);
    GPIOPinWrite(GPIO_PORTN_BASE, GPIO_PIN_4, GPIO_PIN_4);

    //Output CS4 A ADS1299
    GPIOPadConfigSet(GPIO_PORTJ_BASE, GPIO_PIN_1, GPIO_STRENGTH_2MA, GPIO_PIN_TYPE_STD);
    GPIOPinTypeGPIOOutput(GPIO_PORTJ_BASE, GPIO_PIN_1);
    GPIOPinWrite(GPIO_PORTJ_BASE, GPIO_PIN_1, GPIO_PIN_1);

    //Output PWDN1 A ADS1299
    GPIOPadConfigSet(GPIO_PORTQ_BASE, GPIO_PIN_4, GPIO_STRENGTH_2MA, GPIO_PIN_TYPE_STD);
    GPIOPinTypeGPIOOutput(GPIO_PORTQ_BASE, GPIO_PIN_4);
    GPIOPinWrite(GPIO_PORTQ_BASE, GPIO_PIN_4, GPIO_PIN_4);

    //Output PWDN2 A ADS1299
    GPIOPadConfigSet(GPIO_PORTP_BASE, GPIO_PIN_5, GPIO_STRENGTH_2MA, GPIO_PIN_TYPE_STD);
    GPIOPinTypeGPIOOutput(GPIO_PORTP_BASE, GPIO_PIN_5);
    GPIOPinWrite(GPIO_PORTP_BASE, GPIO_PIN_5, GPIO_PIN_5);

    //Output PWDN3 A ADS1299
    GPIOPadConfigSet(GPIO_PORTN_BASE, GPIO_PIN_2, GPIO_STRENGTH_2MA, GPIO_PIN_TYPE_STD);
    GPIOPinTypeGPIOOutput(GPIO_PORTN_BASE, GPIO_PIN_2);
    GPIOPinWrite(GPIO_PORTN_BASE, GPIO_PIN_2, GPIO_PIN_2);

    //Output PWDN4 A ADS1299
    GPIOPadConfigSet(GPIO_PORTN_BASE, GPIO_PIN_5, GPIO_STRENGTH_2MA, GPIO_PIN_TYPE_STD);
    GPIOPinTypeGPIOOutput(GPIO_PORTN_BASE, GPIO_PIN_5);
    GPIOPinWrite(GPIO_PORTN_BASE, GPIO_PIN_5, GPIO_PIN_5);

    //Output START A ADS1299
    GPIOPadConfigSet(GPIO_PORTP_BASE, GPIO_PIN_2, GPIO_STRENGTH_2MA, GPIO_PIN_TYPE_STD);
    GPIOPinTypeGPIOOutput(GPIO_PORTP_BASE, GPIO_PIN_2);
    GPIOPinWrite(GPIO_PORTP_BASE, GPIO_PIN_2, 0);

    //Output DATA RPI READY
    GPIOPadConfigSet(GPIO_PORTA_BASE, GPIO_PIN_1, GPIO_STRENGTH_2MA, GPIO_PIN_TYPE_STD);
    GPIOPinTypeGPIOOutput(GPIO_PORTA_BASE, GPIO_PIN_1);
    GPIOPinWrite(GPIO_PORTA_BASE, GPIO_PIN_1, GPIO_PIN_1);

    //Input DATA RPI READ
    GPIOPinTypeGPIOInput(GPIO_PORTA_BASE, GPIO_PIN_0);
    GPIOIntTypeSet(GPIO_PORTA_BASE, GPIO_PIN_0, GPIO_FALLING_EDGE);
    GPIOIntEnable(GPIO_PORTA_BASE, GPIO_INT_PIN_0);
}

/*
 *  ======== initDebug ========
 *  Configures UART2 (PA6=RX, PA7=TX) at 115200 baud for debug output.
 *  Uses PIOSC (16 MHz) as clock source.
 */
void initDebug(void)
{
    SysCtlPeripheralEnable(SYSCTL_PERIPH_UART2);

    UARTClockSourceSet(UART2_BASE, UART_CLOCK_PIOSC);

    GPIOPinConfigure(GPIO_PA6_U2RX);
    GPIOPinConfigure(GPIO_PA7_U2TX);
    GPIOPinTypeUART(GPIO_PORTA_BASE, GPIO_PIN_6 | GPIO_PIN_7);

    UARTStdioConfig(2, 115200, 16000000);
}

/*
 *  ======== initUSB ========
 *  Initializes USB0 as a CDC (virtual COM port) device.
 *  Incoming bytes are stored in usb_buffer[] by RxHandler and
 *  embedded as per-ADS event markers in the data frame headers.
 */
void initUSB(void)
{
    //
    // Not configured initially.
    //
    g_bUSBConfigured = false;
    uint32_t g_ui32SysClock = SYS_CLOCK_HZ;
    uint32_t ui32PLLRate;

    SysCtlPeripheralEnable(SYSCTL_PERIPH_USB0);

    GPIOPinTypeUSBAnalog(GPIO_PORTB_BASE, GPIO_PIN_0 | GPIO_PIN_1);
    GPIOPinTypeUSBAnalog(GPIO_PORTL_BASE, GPIO_PIN_6 | GPIO_PIN_7);

    //
    // Initialize the transmit and receive buffers.
    //
    USBBufferInit(&g_sTxBuffer);
    USBBufferInit(&g_sRxBuffer);

    //
    // Tell the USB library the CPU clock and the PLL frequency.  This is a
    // new requirement for TM4C129 devices.
    //
    SysCtlVCOGet(SYSCTL_XTAL_25MHZ, &ui32PLLRate);
    USBDCDFeatureSet(0, USBLIB_FEATURE_CPUCLK, &g_ui32SysClock);
    USBDCDFeatureSet(0, USBLIB_FEATURE_USBPLL, &ui32PLLRate);

    //
    // Forcing device mode so that the VBUS and ID pins are not used or
    // monitored by the USB controller. For USB OTG, this function should
    // not be called.  If the USB Host will supply power, and the LaunchPad
    // power jumper is set to "OTG", this function should not be called.
    //
    USBStackModeSet(0, eUSBModeForceDevice, 0);

    //
    // Pass the device information to the USB library and place the device
    // on the bus.
    //
    USBDCDCInit(0, &g_sCDCDevice);
}


/*
 *  ======== initSPI_A ========
 *  Configures SSI2 as SPI master (Mode 1, 4 MHz) for ADS1299 bus.
 *  CS pins are GPIO-controlled (not SSI2FSS).
 */
void initSPI_A(void)
{
    SysCtlPeripheralEnable(SYSCTL_PERIPH_SSI2);

    SSIClockSourceSet(SSI2_BASE, SSI_CLOCK_PIOSC);

    GPIOPinConfigure(GPIO_PD3_SSI2CLK);
    //GPIOPinConfigure(GPIO_PD2_SSI2FSS);
    GPIOPinConfigure(GPIO_PD1_SSI2XDAT0); //MOSI
    GPIOPinConfigure(GPIO_PD0_SSI2XDAT1); //MISO
    GPIOPinTypeSSI(GPIO_PORTD_BASE, GPIO_PIN_3 | GPIO_PIN_1 | GPIO_PIN_0);

    SSIConfigSetExpClk(SSI2_BASE, 16000000, SSI_FRF_MOTO_MODE_1, SSI_MODE_MASTER, 4000000, 8);

    //
    // Enable the SSI for operation
    //
    SSIEnable(SSI2_BASE);
}

/*
 *  ======== initRPI ========
 *  Configures SSI0 as SPI slave (Mode 0, up to 4 MHz) for CM5 link.
 *  Enables TX FIFO interrupt so SSI0IntHandler can push eeeData[] bytes.
 *  PA0 falling-edge interrupt is configured in initGPIO (RPIIntHandler).
 */
void initRPI(void)
{
    SysCtlPeripheralEnable(SYSCTL_PERIPH_SSI0);

    //SSIClockSourceSet(SSI0_BASE, SSI_CLOCK_PIOSC);

    GPIOPinConfigure(GPIO_PA2_SSI0CLK);
    GPIOPinConfigure(GPIO_PA3_SSI0FSS);
    GPIOPinConfigure(GPIO_PA4_SSI0XDAT0); //MOSI
    GPIOPinConfigure(GPIO_PA5_SSI0XDAT1); //MISO

    GPIOPinTypeSSI(GPIO_PORTA_BASE, GPIO_PIN_5 | GPIO_PIN_4 | GPIO_PIN_3 | GPIO_PIN_2);

    SSIConfigSetExpClk(SSI0_BASE, 120000000, SSI_FRF_MOTO_MODE_0, SSI_MODE_SLAVE, 4000000, 8);

    //
    // Enable the SSI for operation
    //
    SSIEnable(SSI0_BASE);

    //
    // Enable SSI0 interrupt on RX FIFO full.
    //
    SSIIntEnable(SSI0_BASE, SSI_TXFF);

    //
    // Enable the SSI0 interrupts on the processor (NVIC).
    //
    IntEnable(INT_SSI0);
}

/*
 *  ======== initSysTick ========
 *  Configures SysTick for 1 kHz (1 ms period).
 *  Used for startup delay (g_ui32Seconds counter in main.c).
 */
void initSysTick(void)
{
    SysTickPeriodSet((SYS_CLOCK_HZ / SYSTICKS_PER_MSECOND) - 1);
    SysTickIntEnable();
    SysTickEnable();
}

//****************************************************************************
//
// Handles CDC driver notifications related to control and setup of the
// device.
//
// \param pvCBData is the client-supplied callback pointer for this channel.
// \param ui32Event identifies the event we are being notified about.
// \param ui32MsgValue is an event-specific value.
// \param pvMsgData is an event-specific pointer.
//
// This function is called by the CDC driver to perform control-related
// operations on behalf of the USB host.  These functions include setting
// and querying the serial communication parameters, setting handshake line
// states and sending break conditions.
//
// \return The return value is event-specific.
//
//****************************************************************************
uint32_t
ControlHandler(void *pvCBData, uint32_t ui32Event,
               uint32_t ui32MsgValue, void *pvMsgData)
{
    //
    // Which event are we being asked to process?
    //
    switch(ui32Event)
    {
        //
        // We are connected to a host and communication is now possible.
        //
        case USB_EVENT_CONNECTED:
            g_bUSBConfigured = true;

            //
            // Flush our buffers.
            //
            USBBufferFlush(&g_sTxBuffer);
            USBBufferFlush(&g_sRxBuffer);

            break;

        //
        // The host has disconnected.
        //
        case USB_EVENT_DISCONNECTED:
            g_bUSBConfigured = false;

            break;

        //
        // Return the current serial communication parameters.
        //
        case USBD_CDC_EVENT_GET_LINE_CODING:
            //GetLineCoding(pvMsgData);
            break;

        //
        // Set the current serial communication parameters.
        //
        case USBD_CDC_EVENT_SET_LINE_CODING:
            //SetLineCoding(pvMsgData);
            break;

        //
        // Set the current serial communication parameters.
        //
        case USBD_CDC_EVENT_SET_CONTROL_LINE_STATE:
            break;

        //
        // Send a break condition on the serial line.
        //
        case USBD_CDC_EVENT_SEND_BREAK:
            //SendBreak(true);
            break;

        //
        // Clear the break condition on the serial line.
        //
        case USBD_CDC_EVENT_CLEAR_BREAK:
            //SendBreak(false);
            break;

        //
        // Ignore SUSPEND and RESUME for now.
        //
        case USB_EVENT_SUSPEND:
        case USB_EVENT_RESUME:
            break;

        //
        // We don't expect to receive any other events.  Ignore any that show
        // up in a release build or hang in a debug build.
        //
        default:
#ifdef DEBUG
            while(1);
#else
            break;
#endif

    }

    return(0);
}

//*****************************************************************************
//
// Handles CDC driver notifications related to the transmit channel (data to
// the USB host).
//
// \param ui32CBData is the client-supplied callback pointer for this channel.
// \param ui32Event identifies the event we are being notified about.
// \param ui32MsgValue is an event-specific value.
// \param pvMsgData is an event-specific pointer.
//
// This function is called by the CDC driver to notify us of any events
// related to operation of the transmit data channel (the IN channel carrying
// data to the USB host).
//
// \return The return value is event-specific.
//
//*****************************************************************************
uint32_t
TxHandler(void *pvCBData, uint32_t ui32Event, uint32_t ui32MsgValue,
          void *pvMsgData)
{
    //
    // Which event have we been sent?
    //
    switch(ui32Event)
    {
        case USB_EVENT_TX_COMPLETE:
            //
            // Since we are using the USBBuffer, we don't need to do anything
            // here.
            //
            break;

        //
        // We don't expect to receive any other events.  Ignore any that show
        // up in a release build or hang in a debug build.
        //
        default:
#ifdef DEBUG
            while(1);
#else
            break;
#endif

    }
    return(0);
}

//*****************************************************************************
//
// Handles CDC driver notifications related to the receive channel (data from
// the USB host).
//
// \param ui32CBData is the client-supplied callback data value for this
//        channel.
// \param ui32Event identifies the event we are being notified about.
// \param ui32MsgValue is an event-specific value.
// \param pvMsgData is an event-specific pointer.
//
// This function is called by the CDC driver to notify us of any events
// related to operation of the receive data channel (the OUT channel carrying
// data from the USB host).
//
// \return The return value is event-specific.
//
//*****************************************************************************
uint32_t
RxHandler(void *pvCBData, uint32_t ui32Event, uint32_t ui32MsgValue,
          void *pvMsgData)
{
    //uint32_t ui32Count;
    uint32_t ui32Read;
    uint8_t ucChar;

    //
    // Which event are we being sent?
    //
    switch(ui32Event)
    {
        //
        // A new packet has been received.
        //
        case USB_EVENT_RX_AVAILABLE:
        {
            //
            // Feed some characters into the UART TX FIFO and enable the
            // interrupt so we are told when there is more space.
            //
            ui32Read = USBBufferRead((tUSBBuffer *)&g_sRxBuffer, &ucChar, 1);

            //
            // Did we get a character?
            //
            if(ui32Read)
            {
				usb_buffer[1] = 0x01;
                usb_buffer[usbCont++] = ucChar;
                if(usbCont == 6)
                    usbCont = 2;
                //UARTprintf("%c", ucChar);
            }
            //USBUARTPrimeTransmit(UART0_BASE);
            //UARTIntEnable(UART0_BASE, UART_INT_TX);
            break;
        }

        //
        // We are being asked how much unprocessed data we have still to
        // process. We return 0 if the UART is currently idle or 1 if it is
        // in the process of transmitting something. The actual number of
        // bytes in the UART FIFO is not important here, merely whether or
        // not everything previously sent to us has been transmitted.
        //
        case USB_EVENT_DATA_REMAINING:
        {
            //
            // Get the number of bytes in the buffer and add 1 if some data
            // still has to clear the transmitter.
            //
            //ui32Count = UARTBusy(UART0_BASE) ? 1 : 0;
            //return(ui32Count);
        }

        //
        // We are being asked to provide a buffer into which the next packet
        // can be read. We do not support this mode of receiving data so let
        // the driver know by returning 0. The CDC driver should not be sending
        // this message but this is included just for illustration and
        // completeness.
        //
        case USB_EVENT_REQUEST_BUFFER:
        {
            return(0);
        }

        //
        // We don't expect to receive any other events.  Ignore any that show
        // up in a release build or hang in a debug build.
        //
        default:
#ifdef DEBUG
            while(1);
#else
            break;
#endif
    }

    return(0);
}
