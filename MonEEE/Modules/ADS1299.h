/**
 * @file    ADS1299.h
 * @brief   ADS1299 driver API - types, register commands, mode functions
 * @author  ber-a
 * @date    Sep 13, 2025
 */

#ifndef MODULES_ADS1299_H_
#define MODULES_ADS1299_H_

#include <stdint.h>
#include <stdbool.h>
#include "Defines_ADS1299.h"

/* =========================================================================
 * Acquisition mode
 *
 * Values are intentionally aligned with the CM5_CMD_* command bytes defined
 * in main.c so the mode byte can be embedded directly in the frame header
 * and the CM5 does not need a separate channel to know the current mode.
 *
 *   ACQ_MODE_IDLE      = 0x05  No conversion running
 *   ACQ_MODE_EEG       = 0x01  Continuous EEG (matches CM5_CMD_START_EEG)
 *   ACQ_MODE_STOP      = 0x02  Stop command    (matches CM5_CMD_STOP)
 *   ACQ_MODE_IMPEDANCE = 0x03  Lead-off sweep  (matches CM5_CMD_START_IMPEDANCE)
 *   ACQ_MODE_TEST      = 0x04  Shorted test    (matches CM5_CMD_START_TEST)
 * ========================================================================= */
typedef enum
{
    ACQ_MODE_IDLE       = 0x05,
    ACQ_MODE_EEG        = 0x01,
    ACQ_MODE_STOP       = 0x02,
    ACQ_MODE_IMPEDANCE  = 0x03,
    ACQ_MODE_TEST       = 0x04
} AcqMode_t;

typedef enum SAMPLE_RATE {
    SAMPLE_RATE_16000,
    SAMPLE_RATE_8000,
    SAMPLE_RATE_4000,
    SAMPLE_RATE_2000,
    SAMPLE_RATE_1000,
    SAMPLE_RATE_500,
    SAMPLE_RATE_250
}SAMPLE_RATE;

/* Register mirror - updated by RREG()/WREG() to track ADS1299 config */
extern uint8_t regData[24];


/* =========================================================================
 * Bit manipulation helpers
 * ========================================================================= */
#define bitSet(x, n)    ((x) |=  (uint8_t)(1U << (n)))
#define bitClear(x, n)  ((x) &= (uint8_t)(~(uint8_t)(1U << (n))))

/* =========================================================================
 * Public API
 * ========================================================================= */

void boardReset(void);
void initADS(void);
void ResetPIN(void);
void resetADS(uint8_t targetSS);

void deactivateChannel(uint8_t N, uint8_t targetSS);
void startTestShorted(void);
void startImpedanceMode(void);
int32_t extractChannelData(uint8_t cha);
void measureAllImpedances(void);
void calculateSendPkPk(void);
void startEEGMode(void);
void stopAcquisition(void);

void RESET(uint8_t targetSS);
void SDATAC(uint8_t targetSS);
uint8_t RREG(uint8_t _address, uint8_t targetSS);
void WREG(uint8_t _address, uint8_t _value, uint8_t target_SS);
void START(uint8_t targetSS);
void STOP(uint8_t targetSS);
void RDATAC(uint8_t targetSS);

uint8_t xfer(uint32_t _data);
void csLow(uint8_t SS);
void csHigh(uint8_t SS);

#endif /* MODULES_ADS1299_H_ */
