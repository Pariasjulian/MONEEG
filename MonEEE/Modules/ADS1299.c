/**
 * @file    ADS1299.c
 * @brief   ADS1299 driver - register commands, mode config, ISRs, impedance
 * @author  ber-a
 * @date    Sep 13, 2025
 *
 * Manages four daisy-chained ADS1299 AFEs (32 channels total) via SSI2
 * master.  Provides mode-configuration functions (EEG, Test, Impedance),
 * low-level SPI register commands (RREG/WREG/RDATAC/SDATAC), and the
 * two real-time ISRs:
 *   - ADSIntHandler : DRDY-triggered, reads all 4 chips into eeeData[]
 *   - SSI0IntHandler: TX-FIFO-triggered, streams eeeData[] to CM5 host
 */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "inc/hw_memmap.h"

#include "driverlib/gpio.h"
#include "driverlib/ssi.h"
#include "driverlib/uart.h"

#include "delay.h"
#include "Defines_ADS1299.h"
#include "ADS1299.h"
#include "../Drivers/Board.h"
#include "utils/uartstdio.h"

/* =========================================================================
 * Module-level variables
 *
 * curSampleRate : Fixed at 250 SPS for all modes
 * numChannels   : 32 (4 chips x 8 ch), used in initADS() for cascading
 * pos           : Current byte index into eeeData[] for SSI0 TX (volatile:
 *                 written by ADSIntHandler, read by SSI0IntHandler)
 * PI_DRDY       : Data-ready flag (volatile: set by ADSIntHandler,
 *                 polled by measureAllImpedances and SSI0IntHandler)
 * regData[24]   : Mirror of the 24 ADS1299 registers (updated by RREG/WREG)
 * ========================================================================= */
SAMPLE_RATE curSampleRate = SAMPLE_RATE_250;
uint8_t numChannels = 32;
volatile uint32_t pos;
volatile bool PI_DRDY = false;
uint8_t regData[24];

extern volatile uint8_t eeeData[140];   /* 112-byte frame + 28B headroom (defined in main.c)  */
extern volatile bool flagIMP;           /* Impedance request flag (defined in main.c)          */
extern int32_t Electrode_Impedance_Ohms[32][32]; /* 32ch x 32 samples (main.c) */

/* =========================================================================
 * boardReset
 * ========================================================================= */
void boardReset(void)
{
    csHigh(BOARD_ADS_ALL);  /* All CS idle-high */
    initSPI_A();            /* SSI2 master      */
    delay_ms(500);
    initADS();
    delay_ms(500);
}

/* =========================================================================
 * initADS ? power-up sequence for all ADS1299 chips
 *
 * Follows the recommended ADS1299 power-up sequence (datasheet ?9.5):
 *   1. Wait >Tpor (>32 ms).
 *   2. Assert RESET pulse.
 *   3. Send RESET command + SDATAC to each chip.
 *   4. Configure CONFIG1 on ADS1 to share its CLK with downstream chips.
 *   5. Apply default channel settings and write to all chips.
 * ========================================================================= */
void initADS(void)
{
    uint8_t cha, dev;

    // recommended power up sequence requiers >Tpor (~32mS)
    delay_ms(50);
    ResetPIN();                 // reset pin connected to ADS ICs
    delay_ms(40);

    /* ADS1 is always present. Reset and stop continuous-read mode.*/
    resetADS(BOARD_ADS1);
    delay_ms(10);

    if(numChannels > 8) {
        /* Enable CLK output from ADS1 ? downstream chips share this clock.*/
        WREG(CONFIG1, (ADS1299_CONFIG1_MULTI_MASTER | (uint8_t)curSampleRate), BOARD_ADS1);
        delay_ms(40);
        resetADS(BOARD_ADS2);
        delay_ms(10);
        /* Disable CLK output from ADS2.*/
        WREG(CONFIG1, (ADS1299_CONFIG1_MULTI_SLAVE | (uint8_t)curSampleRate), BOARD_ADS2);
        delay_ms(40);
    }
    if(numChannels > 16) {
        resetADS(BOARD_ADS3);
        delay_ms(10);
        /* Disable CLK output from ADS3. */
        WREG(CONFIG1, (ADS1299_CONFIG1_MULTI_SLAVE | (uint8_t)curSampleRate), BOARD_ADS3);
        delay_ms(40);
    }
    if(numChannels > 24) {
        resetADS(BOARD_ADS4);
        delay_ms(10);
        /* Disable CLK output from ADS4.*/
        WREG(CONFIG1, (ADS1299_CONFIG1_MULTI_SLAVE | (uint8_t)curSampleRate), BOARD_ADS4);
        delay_ms(40);
    }

    // ---------------------------------------------------------
    // Configure Global Channel Registers (All Devices)
    // ---------------------------------------------------------
    for (dev = BOARD_ADS1; dev <= BOARD_ADS4; dev++) {
        // Channels 1-8 Configuration
        for (cha = CH1SET; cha <= CH8SET; cha++) {
            WREG(cha, 0x68, dev); // PD=0, Gain=24, SRB2=1, MUX=Input normal
            delay_ms(1);
        }

        WREG(MISC1, 0x00, dev);         // MISC1: SRB1 disabled/open
        delay_ms(1);
    }
}

/* =========================================================================
 * ResetPIN ? hardware RESET pulse to all four ADS1299 chips simultaneously
 *
 * Minimum RESET pulse width for ADS1299 is 2 tCLK. We hold for 4 ?s to
 * satisfy the requirement even at the minimum supported CLK frequency.
 * After releasing RESET, wait 18 tCLK before issuing any SPI command.
 * ========================================================================= */
void ResetPIN(void)
{
    GPIOPinWrite(GPIO_PORTP_BASE, GPIO_PIN_3, 0);           //RESET1 A Low
    GPIOPinWrite(GPIO_PORTN_BASE, GPIO_PIN_0, 0);           //RESET2 A Low
    GPIOPinWrite(GPIO_PORTN_BASE, GPIO_PIN_3, 0);           //RESET3 A Low
    GPIOPinWrite(GPIO_PORTJ_BASE, GPIO_PIN_0, 0);           //RESET4 A Low
    delay_us(4);
    GPIOPinWrite(GPIO_PORTP_BASE, GPIO_PIN_3, GPIO_PIN_3);  //RESET1 A High
    GPIOPinWrite(GPIO_PORTN_BASE, GPIO_PIN_0, GPIO_PIN_0);  //RESET2 A High
    GPIOPinWrite(GPIO_PORTN_BASE, GPIO_PIN_3, GPIO_PIN_3);  //RESET3 A High
    GPIOPinWrite(GPIO_PORTJ_BASE, GPIO_PIN_0, GPIO_PIN_0);  //RESET4 A High
    delay_us(20);   /* >18 tCLK before first SPI command (datasheet p.35)   */
}

/* =========================================================================
 * resetADS ? software RESET command + deactivate all 8 channels
 *
 * Issues the RESET opcode (restores all registers to default), stops
 * continuous-data mode, then powers down all 8 channels on the target chip.
 * Removes the channel from both BIAS_SENSP and BIAS_SENSN, and clears its
 * lead-off detection bits.
 * ========================================================================= */
void resetADS(uint8_t targetSS)
{
    uint8_t chan;

    RESET(targetSS);  // Restore all registers to default
    delay_ms(1);
    SDATAC(targetSS); // Exit RDATAC so registers can be written
    delay_ms(10);

    // turn off all channels
    for (chan = 1U; chan <= 8U; chan++)
    {
        deactivateChannel(chan, targetSS);
    }

    /* Remove from positive bias generation */
    WREG(BIAS_SENSP, 0x00, targetSS);
    delay_ms(1);

    /* Remove from negative bias generation */
    WREG(BIAS_SENSN, 0x00, targetSS);
    delay_ms(1);

    /* Lead-off positive disabled. */
    WREG(LOFF_SENSP, 0x00, targetSS);
    delay_ms(1);

    /* Lead-off negative disabled. */
    WREG(LOFF_SENSN, 0x00, targetSS);
    delay_ms(1);
}

/* =========================================================================
 * deactivateChannel ? power down one channel and remove it from BIAS/LOFF
 *
 * @param N        1-based channel number (1?8)
 * @param targetSS Chip-select identifier (BOARD_ADS1..4)
 *
 * Sets CHnSET.PD = 1 (power-down), clears SRB2 for that channel.
 * ========================================================================= */
void deactivateChannel(uint8_t N, uint8_t targetSS)
{
    uint8_t setting;

    N = constrain(N - 1U, 0U, 7U);       // Convert to 0-based and clamp to valid range

    /* Power down channel: set PD bit, clear SRB2 bit                       */
    setting = RREG(CH1SET + N, targetSS);
    delay_ms(1);
    bitSet(setting, 7);                                 // PD  = 1 -> power down the channel MUX
    bitClear(setting, 3);                               // SRB2 = 0 -> disconnect from SRB2 reference
    WREG(CH1SET + N, setting, targetSS);
    delay_ms(1);
}

/* =========================================================================
 * startTestShorted - configure all ADS1299 for shorted-input noise test
 *
 * ADS1 = master (generates CLK + BIAS), ADS2-4 = slaves.
 * All 32 channels: Gain=24, SRB2=closed, MUX=Input Shorted, 250 SPS.
 * Starts continuous RDATAC streaming; ADSIntHandler takes over.
 * ========================================================================= */
void startTestShorted(void)
{
    uint8_t dev, cha;

    // ---------------------------------------------------------
    // Reset and Halt Data Conversion on ALL chips
    // ---------------------------------------------------------
    for (dev = BOARD_ADS1; dev <= BOARD_ADS4; dev++) {
        RESET(dev);
        delay_ms(1);
        SDATAC(dev);
        delay_ms(1);
    }

    // -------------------------------------------------------------------------
    // Configure Device 1 (Master: Generates Clock and Bias)
    // -------------------------------------------------------------------------
    WREG(CONFIG1, 0xF6, BOARD_ADS1); // ~DAISY_EN=1, ~CLK_EN=1 (Output clk), DR=110 (250 SPS)
    delay_ms(1);
    WREG(CONFIG3, 0xEC, BOARD_ADS1); // PD_REFBUF=1, BIASREF_INT=1, PD_BIAS=1 (Power BIAS amp)
    delay_ms(1);

    // Configure all 8 channels for Device 1
    for (cha = CH1SET; cha <= CH8SET; cha++) {
        WREG(cha, 0x69, BOARD_ADS1); // PD=0, Gain=24, SRB2=1, MUX=Input shorted
        delay_ms(1);
    }
    WREG(MISC1, 0x00, BOARD_ADS1);   // SRB1=0 (Open)
    delay_ms(1);

    // -------------------------------------------------------------------------
    // Configure Devices 2, 3, and 4 (Slaves: Accept Clock, Bias powered down)
    // -------------------------------------------------------------------------
    for (dev = BOARD_ADS2; dev <= BOARD_ADS4; dev++) {
        WREG(CONFIG1, 0xD6, dev); // ~DAISY_EN=1, ~CLK_EN=0 (Disabled), DR=110 (250 SPS)
        delay_ms(1);
        WREG(CONFIG3, 0xE8, dev); // PD_REFBUF=1, BIASREF_INT=1, PD_BIAS=0 (Power BIAS amp)
        delay_ms(1);

        // Configure all 8 channels for the current Slave
        for (cha = CH1SET; cha <= CH8SET; cha++) {
            WREG(cha, 0x69, dev); // PD=0, Gain=24, SRB2=1, MUX=Input shorted
            delay_ms(1);
        }
        WREG(MISC1, 0x00, dev);   // SRB1=0 (Open)
        delay_ms(1);
    }

    // -------------------------------------------------------------------------
    // Start Conversions and Enter Live Mode (Optional / Readiness)
    // -------------------------------------------------------------------------
    RDATAC(BOARD_ADS_ALL);
    START(BOARD_ADS_ALL);

    UARTprintf("MonEEE: Test short stream START\r\n");
}

/*
 * startImpedanceMode ? enables impedance detection on the N side of all
 * channels of the four ADS1299 devices.
 *
 * Hardware topology:
 *   - Active electrode -> IN-  (N side of each channel)
 *   - SRB2             -> IN+  (shared reference for all channels,
 *                              controlled by the SRB2 bit in CHnSET)
 */
void startImpedanceMode(void)
{
    uint8_t dev, cha;

    // ---------------------------------------------------------
    // Reset and Halt Data Conversion on ALL chips
    // ---------------------------------------------------------
    for (dev = BOARD_ADS1; dev <= BOARD_ADS4; dev++) {
        RESET(dev);
        delay_ms(1);
        SDATAC(dev);
        delay_ms(1);
    }

    // ---------------------------------------------------------
    // Configure Device 1 (Master)
    // ---------------------------------------------------------
    WREG(CONFIG1, 0xF6, BOARD_ADS1);    // ~DAISY_EN=1, ~CLK_EN=1 (CLK output enabled), DR=110 (250 SPS)
    delay_ms(1);
    WREG(CONFIG3, 0xEC, BOARD_ADS1);    // PD_REFBUF=1, BIASREF_INT=1, PD_BIAS=1 (Power BIAS amp)
    delay_ms(1);
    WREG(BIAS_SENSN, 0xFF, BOARD_ADS1); // BIAS_SENSN: Route all INxN electrodes on Dev 1 to Bias drive
    delay_ms(1);

    // -------------------------------------------------------------------------
    // Configure Devices 2, 3, and 4 (Slaves: Accept Clock, Bias powered down)
    // -------------------------------------------------------------------------
    for (dev = BOARD_ADS2; dev <= BOARD_ADS4; dev++) {
        WREG(CONFIG1, 0xD6, dev);    // ~DAISY_EN=1, ~CLK_EN=0 (Disabled), DR=110 (250 SPS)
        delay_ms(1);
        WREG(CONFIG3, 0xE8, dev);    // PD_REFBUF=1, BIASREF_INT=1, PD_BIAS=0 (Power BIAS amp)
        delay_ms(1);
        WREG(BIAS_SENSN, 0x00, dev); // BIAS_SENSN: Do not route electrodes to the disabled bias amp
        delay_ms(1);
    }

    // ---------------------------------------------------------
    // Configure Global Registers (All Devices)
    // ---------------------------------------------------------
    for (dev = BOARD_ADS1; dev <= BOARD_ADS4; dev++) {
        WREG(LOFF, 0x0A, dev); // LOFF: AC impedance at 95% threshold, 6uA current, 31.2 Hz frequency
        delay_ms(1);

        // Channels 1-8 Configuration
        for (cha = CH1SET; cha <= CH8SET; cha++) {
            WREG(cha, 0x08, dev); // PD=0, Gain=1, SRB2=1, MUX=Input normal
            delay_ms(1);
        }


        WREG(BIAS_SENSP, 0x00, dev);    // BIAS_SENSP: Do not route INxP (SRB2) to bias
        delay_ms(1);
        WREG(LOFF_SENSP, 0x00, dev);    // LOFF_SENSP: Do not inject AC current into the common SRB2 reference
        delay_ms(1);
        WREG(LOFF_SENSN, 0x00, dev);    // LOFF_SENSN: Not inject AC current into all INxN electrodes
        delay_ms(1);
        WREG(MISC1, 0x00, dev);         // MISC1: SRB1 disabled/open
        delay_ms(1);

    }

    WREG(LOFF_SENSN, 0x01, BOARD_ADS1);    // LOFF_SENSN: Enable lead-off current on IN1N of ADS1 (initial channel)
    delay_ms(1);

    // -------------------------------------------------------------------------
    // Start Conversions and Enter Live Mode (Optional / Readiness)
    // -------------------------------------------------------------------------
    RDATAC(BOARD_ADS_ALL);
    START(BOARD_ADS_ALL);

    UARTprintf("MonEEE: Impedance mode START (N-channel only)\r\n");
}

/* =========================================================================
 * extractChannelData - read one 24-bit sample from eeeData[]
 *
 * @param cha  Global channel index (0-31)
 * @return     Sign-extended 32-bit value from the 3-byte sample
 *
 * Maps the global channel number to the correct byte offset in eeeData[],
 * accounting for the 4-byte header in each 28-byte ADS block.
 *   Chip 0 (ADS1): base = 4   (bytes 0-3 are header)
 *   Chip 1 (ADS2): base = 32  (bytes 28-31 are header)
 *   Chip 2 (ADS3): base = 60  (bytes 56-59 are header)
 *   Chip 3 (ADS4): base = 88  (bytes 84-87 are header)
 * ========================================================================= */
int32_t extractChannelData(uint8_t cha) {
    uint8_t chip = cha / 8;
    uint8_t local_cha = cha % 8;
    uint16_t base_idx = 0;

    // Mapping base indices based on your ADSIntHandler structure
    if (chip == 0) base_idx = 4;
    else if (chip == 1) base_idx = 32;
    else if (chip == 2) base_idx = 60;
    else if (chip == 3) base_idx = 88;

    uint16_t idx = base_idx + (local_cha * 3);

    // Reconstruct 24-bit value from 3 bytes (MSB first)
    int32_t val = (eeeData[idx] << 16) | (eeeData[idx+1] << 8) | eeeData[idx+2];

    // Sign extend to 32 bits if the 24th bit (sign bit) is 1
    if (val & 0x00800000) {
        val |= 0xFF000000;
    }

    return val;
}

/* =========================================================================
 * measureAllImpedances - blocking impedance sweep (called from main loop)
 *
 * Sequentially sweeps all 32 channels.  For each channel:
 *   1. Stops RDATAC, writes LOFF_SENSN to inject 6 uA AC on that IN-
 *   2. Restarts RDATAC, discards 10 settling samples (~40 ms)
 *   3. Collects 32 raw samples into Electrode_Impedance_Ohms[ch][32]
 *
 * Total sweep time: 32 ch x (40 ms settle + 128 ms capture) ~ 5.4 s
 * After sweep, disables all LOFF currents and halts conversion.
 * ========================================================================= */
void measureAllImpedances(void)
{
    uint8_t cha, sample, dev, discard;
    uint8_t target_chip;
    uint8_t pin_mask;

    for (cha = 0; cha < 32; cha++) {
        target_chip = (cha / 8) + 1;    // Chip 1, 2, 3, or 4
        pin_mask = 1 << (cha % 8);      // Bit 0-7 corresponding to IN1-IN8

        // Stop continuous mode to send SPI register commands
        SDATAC(BOARD_ADS_ALL);
        delay_us(20);

        // Clear all LOFF_SENSN registers, then set ONLY the current channel
        for (dev = BOARD_ADS1; dev <= BOARD_ADS4; dev++) {
            if (dev == target_chip) {
                WREG(LOFF_SENSN, pin_mask, dev); // Enable current on target electrode
                delay_us(20);
            } else {
                WREG(LOFF_SENSN, 0x00, dev);     // Disable current on all others
                delay_us(20);
            }
        }

        // Restart continuous conversions
        RDATAC(BOARD_ADS_ALL);
        delay_us(20);

        PI_DRDY = false;

        // Discard the first 10 samples (40 ms) to allow the AC signal
        // and digital filters to settle after injecting current.
        for (discard = 0; discard < 10; discard++) {
            while (!PI_DRDY); // Wait for your interrupt handler to flag new data
            PI_DRDY = false;
        }

        // Collect 32 samples for the active channel
        for (sample = 0; sample < 32; sample++) {
            while (!PI_DRDY); // Wait for new frame
            PI_DRDY = false;

            // Extract and store the raw sample
            Electrode_Impedance_Ohms[cha][sample] = extractChannelData(cha);
        }
    }

    // Cleanup: Disable all LOFF currents and return to normal EEG mode
    SDATAC(BOARD_ADS_ALL);
    STOP(BOARD_ADS_ALL);
    delay_ms(1);

    for (dev = BOARD_ADS1; dev <= BOARD_ADS4; dev++) {
        WREG(LOFF_SENSN, 0x00, dev);
        delay_us(20);
    }

    delay_ms(6);
}

/* =========================================================================
 * calculateSendPkPk - compute peak-to-peak and send to CM5
 *
 * For each of the 32 channels, finds max and min across the 32 collected
 * impedance samples, computes (max - min) as a 24-bit unsigned code,
 * and packs it into eeeData[] in the standard frame format.  Then
 * triggers the CM5 data-ready signal (PA1 LOW) exactly as ADSIntHandler
 * would for a normal data frame.
 * ========================================================================= */
void calculateSendPkPk(void) {
    uint8_t cha, sample;
    int32_t max_val, min_val;
    uint32_t pk_pk_code;
    uint8_t chip, local_cha;
    uint16_t base_idx, idx;

    // Rebuild the header bytes to mimic a normal data frame
    // so the CM5 parses it correctly.
    eeeData[0] = 0x00; eeeData[1] = 0x7E; eeeData[2] = usb_buffer[1]; eeeData[3] = usb_buffer[2];
    eeeData[28] = 0x00; eeeData[29] = 0x7D; eeeData[30] = usb_buffer[1]; eeeData[31] = usb_buffer[3];
    eeeData[56] = 0x00; eeeData[57] = 0x7C; eeeData[58] = usb_buffer[1]; eeeData[59] = usb_buffer[4];
    eeeData[84] = 0x00; eeeData[85] = 0x7B; eeeData[86] = usb_buffer[1]; eeeData[87] = usb_buffer[5];

    // Calculate peak-to-peak code for each of the 32 channels
    for (cha = 0; cha < 32; cha++) {
        max_val = Electrode_Impedance_Ohms[cha][0];
        min_val = Electrode_Impedance_Ohms[cha][0];

        // Scan the 32 samples to find the highest and lowest points
        for (sample = 1; sample < 32; sample++) {
            if (Electrode_Impedance_Ohms[cha][sample] > max_val) {
                max_val = Electrode_Impedance_Ohms[cha][sample];
            }
            if (Electrode_Impedance_Ohms[cha][sample] < min_val) {
                min_val = Electrode_Impedance_Ohms[cha][sample];
            }
        }

        // Calculate peak-to-peak difference
        // The result is an absolute magnitude that fits within 24 bits
        pk_pk_code = (uint32_t)(max_val - min_val);

        // Map the calculated code back to the eeeData buffer
        chip = (cha / 8) + 1;
        local_cha = cha % 8;
        base_idx = 0;

        if (chip == BOARD_ADS1) base_idx = 4;
        else if (chip == BOARD_ADS2) base_idx = 32;
        else if (chip == BOARD_ADS3) base_idx = 60;
        else if (chip == BOARD_ADS4) base_idx = 88;

        idx = base_idx + (local_cha * 3);

        // Pack the 24-bit value into the 3 bytes (MSB first)
        eeeData[idx]     = (uint8_t)((pk_pk_code >> 16) & 0xFF);
        eeeData[idx + 1] = (uint8_t)((pk_pk_code >> 8) & 0xFF);
        eeeData[idx + 2] = (uint8_t)(pk_pk_code & 0xFF);
    }

    // Trigger the CM5 exactly as done in ADSIntHandler
    pos = 0;
    memset((void *)usb_buffer, 0, sizeof(usb_buffer));
    usbCont = 2;
    PI_DRDY = true;

    // Pull the GPIO pin low to alert the CM5 that data is ready to be clocked out
    GPIOPinWrite(GPIO_PORTA_BASE, GPIO_PIN_1, 0);
}

/*
 * stopAcquisition ? places all ADS1299 in SDATAC/STOP regardless of mode.
 * Safe to call from any mode transition.
 */
void stopAcquisition(void)
{
    SDATAC(BOARD_ADS_ALL);
    STOP(BOARD_ADS_ALL);
}

/*
 * startEEGMode ? enables continuous EEG data acquisition on all four ADS1299.
 * Restores default channel settings (no lead-off injection) before starting.
 */
void startEEGMode(void)
{
    uint8_t dev, cha;

    // ---------------------------------------------------------
    // Reset and Halt Data Conversion on ALL chips
    // ---------------------------------------------------------
    for (dev = BOARD_ADS1; dev <= BOARD_ADS4; dev++) {
        RESET(dev);
        delay_ms(1);
        SDATAC(dev);
        delay_ms(1);
    }

    // ==========================================================
    // CONFIGURE MASTER DEVICE (Provides CLK and BIAS)
    // ==========================================================
    WREG(CONFIG1, 0xB6, BOARD_ADS1); // CONFIG1: Parallel, CLK OUT En, 250 SPS
    delay_ms(1);
    WREG(CONFIG3, 0xEC, BOARD_ADS1); // CONFIG3: Int ref buffer ON, Int bias ref, Bias buffer ON
    delay_ms(1);
    WREG(LOFF, 0x00, BOARD_ADS1);    // LOFF: Lead-off disabled
    delay_ms(1);

    // Write CH1SET to CH8SET (Addresses 0x05 to 0x0C)
    for (cha = CH1SET; cha <= CH8SET; cha++) {
        WREG(cha, 0x68, BOARD_ADS1); // Power ON, Gain 24x, SRB2 closed, Normal input
        delay_ms(1);
    }

    WREG(BIAS_SENSP, 0x00, BOARD_ADS1); // BIAS_SENSP: INxP disconnected from Bias
    delay_ms(1);
    WREG(BIAS_SENSN, 0xFF, BOARD_ADS1); // BIAS_SENSN: All 8 INxN electrodes routed to Bias derivation
    delay_ms(1);
    WREG(LOFF_SENSP, 0x00, BOARD_ADS1); // LOFF_SENSP: Default
    delay_ms(1);
    WREG(LOFF_SENSN, 0x00, BOARD_ADS1); // LOFF_SENSN: Default
    delay_ms(1);
    WREG(MISC1, 0x00, BOARD_ADS1);      // MISC1: SRB1 open
    delay_ms(1);

    // ==========================================================
    // CONFIGURE SLAVE DEVICES (Accept CLK, BIAS Disabled)
    // ==========================================================
    for (dev = BOARD_ADS2; dev <= BOARD_ADS4; dev++) {
        WREG(CONFIG1, 0x96, dev); // CONFIG1: Parallel, CLK OUT Dis, 250 SPS
        delay_ms(1);
        WREG(CONFIG3, 0xE8, dev); // CONFIG3: Int ref buffer ON, Int bias ref, Bias buffer DOWN
        delay_ms(1);
        WREG(LOFF, 0x00, dev);    // LOFF: Lead-off disabled
        delay_ms(1);

        // Write CH1SET to CH8SET (Addresses 0x05 to 0x0C)
        for (cha = CH1SET; cha <= CH8SET; cha++) {
            WREG(cha, 0x68, dev); // PD=0, Gain=24, SRB2=1, MUX=Input normal
            delay_ms(1);
        }

        WREG(BIAS_SENSP, 0x00, dev); // BIAS_SENSP: INxP disconnected from Bias
        delay_ms(1);
        WREG(BIAS_SENSN, 0x00, dev); // BIAS_SENSN: All 8 INxN electrodes routed to Bias derivation
        delay_ms(1);
        WREG(LOFF_SENSP, 0x00, dev); // LOFF_SENSP: Default
        delay_ms(1);
        WREG(LOFF_SENSN, 0x00, dev); // LOFF_SENSN: Default
        delay_ms(1);
        WREG(MISC1, 0x00, dev);      // MISC1: SRB1 open
        delay_ms(1);
    }

    // ==========================================================
    // START CONVERSIONS
    // ==========================================================

    RDATAC(BOARD_ADS_ALL);
    START(BOARD_ADS_ALL);

    UARTprintf("MonEEE: EEG stream START\r\n");
}

/* =========================================================================
 * ADS1299 SPI command wrappers
 * ========================================================================= */
void RESET(uint8_t targetSS)
{
    csLow(targetSS);
    xfer(_RESET);
    csHigh(targetSS);
    delay_us(12);       // >18 tCLK cycles required (datasheet p.35)
}

void SDATAC(uint8_t targetSS)
{
  csLow(targetSS);
  xfer(_SDATAC);
  csHigh(targetSS);
  delay_us(10);         // >4 tCLK cycles required (datasheet p.37)
}

/* RREG - read one register at _address (opcode 001rrrrr + 0x00 + dummy) */
uint8_t RREG(uint8_t _address, uint8_t targetSS)
{
    uint32_t opcode1 = _address + 0x00000020;       //  RREG expects 001rrrrr where rrrrr = _address
    csLow(targetSS);                                //  open SPI
    xfer(opcode1);                                  //  opcode1
    xfer(0x00000000);                               //  opcode2
    regData[_address] = xfer(0x00000000);           //  update mirror location with returned byte
    csHigh(targetSS);                               //  close SPI

    return regData[_address]; // return requested register value
}

/* WREG - write one register at _address (opcode 010rrrrr + 0x00 + value) */
void WREG(uint8_t _address, uint8_t _value, uint8_t target_SS)
{                                 //  Write ONE register at _address
  uint32_t opcode1 = _address + 0x00000040;         //  WREG expects 010rrrrr where rrrrr = _address
  csLow(target_SS);                                 //  open SPI
  xfer(opcode1);                                    //  Send WREG command & address
  xfer(0x00000000);                                 //  Send number of registers to read -1
  xfer(_value);                                     //  Write the value to the register
  csHigh(target_SS);              //  close SPI
  regData[_address] = _value;     //  update the mirror array
}

void START(uint8_t targetSS)
{
    csLow(targetSS);
    xfer(_START);
    csHigh(targetSS);
}

void STOP(uint8_t targetSS)
{
    csLow(targetSS);
    xfer(_STOP);
    csHigh(targetSS);
}

void RDATAC(uint8_t targetSS)
{
    csLow(targetSS);
    xfer(_RDATAC);
    csHigh(targetSS);
    delay_us(3);
}

/* =========================================================================
 * xfer ? single byte exchange on SSI2 (ADS SPI bus, master side)
 * ========================================================================= */
uint8_t xfer(uint32_t _data)
{
    uint32_t rxByte = 0;

    SSIDataPut(SSI2_BASE, _data);
    while(SSIBusy(SSI2_BASE));
    while(SSIDataGetNonBlocking(SSI2_BASE, &rxByte));

    return (uint8_t)rxByte;
}

/* =========================================================================
 * csLow / csHigh ? chip-select helpers
 * ========================================================================= */
void csLow(uint8_t SS)
{
    switch (SS)
    {
        case BOARD_ADS1:
            GPIOPinWrite(GPIO_PORTD_BASE, GPIO_PIN_2, 0);
        break;
        case BOARD_ADS2:
            GPIOPinWrite(GPIO_PORTN_BASE, GPIO_PIN_1, 0);
        break;
        case BOARD_ADS3:
            GPIOPinWrite(GPIO_PORTN_BASE, GPIO_PIN_4, 0);
        break;
        case BOARD_ADS4:
            GPIOPinWrite(GPIO_PORTJ_BASE, GPIO_PIN_1, 0);
        break;
        case BOARD_ADS_ALL:
            GPIOPinWrite(GPIO_PORTD_BASE, GPIO_PIN_2, 0);
            GPIOPinWrite(GPIO_PORTN_BASE, GPIO_PIN_1, 0);
            GPIOPinWrite(GPIO_PORTN_BASE, GPIO_PIN_4, 0);
            GPIOPinWrite(GPIO_PORTJ_BASE, GPIO_PIN_1, 0);
        break;
        default:
        break;
    }
}

void csHigh(uint8_t SS)
{
    switch (SS)
    {
        case BOARD_ADS1:
            GPIOPinWrite(GPIO_PORTD_BASE, GPIO_PIN_2, GPIO_PIN_2);
        break;
        case BOARD_ADS2:
            GPIOPinWrite(GPIO_PORTN_BASE, GPIO_PIN_1, GPIO_PIN_1);
        break;
        case BOARD_ADS3:
            GPIOPinWrite(GPIO_PORTN_BASE, GPIO_PIN_4, GPIO_PIN_4);
        break;
        case BOARD_ADS4:
            GPIOPinWrite(GPIO_PORTJ_BASE, GPIO_PIN_1, GPIO_PIN_1);
        break;
        case BOARD_ADS_ALL:
            GPIOPinWrite(GPIO_PORTD_BASE, GPIO_PIN_2, GPIO_PIN_2);
            GPIOPinWrite(GPIO_PORTN_BASE, GPIO_PIN_1, GPIO_PIN_1);
            GPIOPinWrite(GPIO_PORTN_BASE, GPIO_PIN_4, GPIO_PIN_4);
            GPIOPinWrite(GPIO_PORTJ_BASE, GPIO_PIN_1, GPIO_PIN_1);
        break;
        default:
        break;
    }
}

/* =========================================================================
 * ADSIntHandler - DRDY interrupt (PP4, falling edge)
 *
 * Triggered at 250 Hz when all four ADS1299 chips have new data.
 * Reads 27 bytes from each chip (3 status + 8ch x 3B), injects
 * frame markers [0x00][0x7x] and USB event bytes, then signals
 * the CM5 by pulling PA1 LOW (unless in impedance sweep mode).
 *
 * eeeData[] layout after this ISR:
 *   [ 0..27] ADS1: [0x00][0x7E][USB_H][USB_L] + CH0-CH7  (24B)
 *   [28..55] ADS2: [0x00][0x7D][USB_H][USB_L] + CH8-CH15 (24B)
 *   [56..83] ADS3: [0x00][0x7C][USB_H][USB_L] + CH16-CH23(24B)
 *   [84..111]ADS4: [0x00][0x7B][USB_H][USB_L] + CH24-CH31(24B)
 * ========================================================================= */
void ADSIntHandler(void)
{
    uint8_t i;
    uint32_t stemp;
	uint32_t ui32Status;
	ui32Status = GPIOIntStatus(GPIO_PORTP_BASE, true) & GPIO_PIN_4;	
	
	GPIOIntClear(GPIO_PORTP_BASE, GPIO_PIN_4);  // Clear interrupt flag

    if (ui32Status)
    {
        csLow(BOARD_ADS1);
        for(i = 1; i < 28; i++)
        {
            eeeData[i] = 0x00;
            SSIDataPut(SSI2_BASE, 0x00000000);
            while(SSIBusy(SSI2_BASE));
            while(SSIDataGetNonBlocking(SSI2_BASE, &stemp));

            if(i == 3) {
                eeeData[0] = 0x00;
                eeeData[1] = 0x7E;
                eeeData[2] = usb_buffer[1];
                eeeData[3] = usb_buffer[2];
            } else {
                eeeData[i] = (uint8_t)stemp;
            }

        }
        csHigh(BOARD_ADS1);
		csLow(BOARD_ADS2);
        for(i = 29; i < 56; i++)
        {
            eeeData[i] = 0x00;
            SSIDataPut(SSI2_BASE, 0x00000000);
            while(SSIBusy(SSI2_BASE));
            while(SSIDataGetNonBlocking(SSI2_BASE, &stemp));

            if(i == 31) {
                eeeData[28] = 0x00;
                eeeData[29] = 0x7D;
                eeeData[30] = usb_buffer[1];
                eeeData[31] = usb_buffer[3];
            } else {
                eeeData[i] = (uint8_t)stemp;
            }

        }
        csHigh(BOARD_ADS2);
        csLow(BOARD_ADS3);
        for(i = 57; i < 84; i++)
        {
            eeeData[i] = 0x00;
            SSIDataPut(SSI2_BASE, 0x00000000);
            while(SSIBusy(SSI2_BASE));
            while(SSIDataGetNonBlocking(SSI2_BASE, &stemp));

            if(i == 59) {
                eeeData[56] = 0x00;
                eeeData[57] = 0x7C;
                eeeData[58] = usb_buffer[1];
                eeeData[59] = usb_buffer[4];
            } else {
                eeeData[i] = (uint8_t)stemp;
            }

        }
        csHigh(BOARD_ADS3);
        csLow(BOARD_ADS4);
        for(i = 85; i < 112; i++)
        {
            eeeData[i] = 0x00;
            SSIDataPut(SSI2_BASE, 0x00000000);
            while(SSIBusy(SSI2_BASE));
            while(SSIDataGetNonBlocking(SSI2_BASE, &stemp));

            if(i == 87) {
                eeeData[84] = 0x00;
                eeeData[85] = 0x7B;
                eeeData[86] = usb_buffer[1];
                eeeData[87] = usb_buffer[5];
            } else {
                eeeData[i] = (uint8_t)stemp;
            }

        }
        csHigh(BOARD_ADS4);
        pos = 0;
		memset((void *)usb_buffer, 0, sizeof(usb_buffer));
		usbCont = 2;
		if(flagIMP) {
		    PI_DRDY = true;
		} else {
		    PI_DRDY = true;
		    GPIOPinWrite(GPIO_PORTA_BASE, GPIO_PIN_1, 0);
		}
    }
}

/* =========================================================================
 * SSI0IntHandler - SSI0 TX FIFO interrupt
 *
 * Fires when the SSI0 TX FIFO has space.  Feeds one byte from
 * eeeData[pos++] into the FIFO for the CM5 to clock out.
 * On the first call after ADSIntHandler, releases PA1 HIGH to
 * acknowledge the data-ready signal.
 * ========================================================================= */
void SSI0IntHandler(void)
{
    uint32_t ui32Status;
    uint32_t stemp = 0;

    ui32Status = SSIIntStatus(SSI0_BASE, true);
    SSIIntClear(SSI0_BASE, ui32Status);

    if(PI_DRDY) {
        GPIOPinWrite(GPIO_PORTA_BASE, GPIO_PIN_1, GPIO_PIN_1);
        PI_DRDY = false;
    }

    if (ui32Status & SSI_TXFF)
    {
        stemp |= eeeData[pos++];
        SSIDataPutNonBlocking(SSI0_BASE, stemp);
    }
}
