/**
 * @file    Defines_ADS1299.h
 * @brief   ADS1299 register addresses, SPI opcodes, and configuration constants
 * @author  ber-a
 * @date    Sep 16, 2025
 *
 * All values sourced from the ADS1299 datasheet (SBAS499C).
 */

#ifndef MODULES_DEFINES_ADS1299_H_
#define MODULES_DEFINES_ADS1299_H_

/* Chip-select identifiers for csLow()/csHigh() */
#define BOARD_ADS_ALL   0         // All ADS chip select
#define BOARD_ADS1      1         // ADS1 chip select
#define BOARD_ADS2      2         // ADS2 chip select
#define BOARD_ADS3      3         // ADS3 chip select
#define BOARD_ADS4      4         // ADS4 chip select


/* ADS1299 SPI command opcodes (datasheet Table 10, p.35) */
#define _WAKEUP     0x00000002    // Wake-up from standby mode
#define _STANDBY    0x00000004    // Enter Standby mode
#define _RESET      0x00000006    // Reset the device registers to default
#define _START      0x00000008    // Start and restart (synchronize) conversions
#define _STOP       0x0000000A    // Stop conversion
#define _RDATAC     0x00000010    // Enable Read Data Continuous mode (default mode at power-up)
#define _SDATAC     0x00000011    // Stop Read Data Continuous mode
#define _RDATA      0x00000012    // Read data by command supports multiple read back

/* ADS1299 register addresses (datasheet Table 9, p.33) */
#define ADS_ID      0x0000003E    // product ID for ADS1299
#define ID_REG      0x00000000    // this register contains ADS_ID
#define CONFIG1     0x00000001
#define CONFIG2     0x00000002
#define CONFIG3     0x00000003
#define LOFF        0x00000004
#define CH1SET      0x00000005
#define CH2SET      0x00000006
#define CH3SET      0x00000007
#define CH4SET      0x00000008
#define CH5SET      0x00000009
#define CH6SET      0x0000000A
#define CH7SET      0x0000000B
#define CH8SET      0x0000000C
#define BIAS_SENSP  0x0000000D
#define BIAS_SENSN  0x0000000E
#define LOFF_SENSP  0x0000000F
#define LOFF_SENSN  0x00000010
#define LOFF_FLIP   0x00000011
#define LOFF_STATP  0x00000012
#define LOFF_STATN  0x00000013
#define GPIO        0x00000014
#define MISC1       0x00000015
#define MISC2       0x00000016
#define CONFIG4     0x00000017

/* CHnSET register bit-field indices (for channelSettings[][] arrays) */
#define POWER_DOWN      0
#define GAIN_SET        1
#define INPUT_TYPE_SET  2
#define BIASP_SET       3
#define BIASN_SET       4
#define SRB2_SET        5
#define SRB1_SET        6
#define YES             0x01
#define NO              0x00

/* PGA gain settings (CHnSET[6:4] field) */
#define ADS_GAIN01  0b00000000 // 0x00
#define ADS_GAIN02  0b00010000 // 0x10
#define ADS_GAIN04  0b00100000 // 0x20
#define ADS_GAIN06  0b00110000 // 0x30
#define ADS_GAIN08  0b01000000 // 0x40
#define ADS_GAIN12  0b01010000 // 0x50
#define ADS_GAIN24  0b01100000 // 0x60

/* MUX input type settings (CHnSET[2:0] field) */
#define ADSINPUT_NORMAL     0b00000000
#define ADSINPUT_SHORTED    0b00000001
#define ADSINPUT_BIAS_MEAS  0b00000010
#define ADSINPUT_MVDD       0b00000011
#define ADSINPUT_TEMP       0b00000100
#define ADSINPUT_TESTSIG    0b00000101
#define ADSINPUT_BIAS_DRP   0b00000110
#define ADSINPUT_BIAL_DRN   0b00000111

/*
 * Per-chip channel count. Each ADS1299 manages 8 channels. The 4-chip board
 * has 32 channels total, but channelSettings / leadOffSettings / useInBias /
 * useSRB2 arrays are indexed 0-7 and applied identically to every chip.
 * Using 8 here avoids wasting 24 entries of dead stack/BSS.
 */
#define NUMBER_OF_CHANNELS_PER_ADS  8U
#define NUMBER_OF_CHANNEL_SETTINGS  7U
#define NUMBER_OF_LEAD_OFF_SETTINGS 2U

/* Legacy alias kept for compatibility */
#define NUMBER_OF_CHANNELS_DEFAULT  8U

#define ADS1299_CONFIG1_MULTI_MASTER       0b11110000
#define ADS1299_CONFIG1_MULTI_SLAVE        0b11010000

/* Lead-off current magnitude and frequency settings (LOFF register) */
#define LOFF_MAG_6NA        0b00000000
#define LOFF_MAG_24NA       0b00000100
#define LOFF_MAG_6UA        0b00001000
#define LOFF_MAG_24UA       0b00001100
#define LOFF_FREQ_DC        0b00000000
#define LOFF_FREQ_7p8HZ     0b00000001
#define LOFF_FREQ_31p2HZ    0b00000010
#define LOFF_FREQ_FS_4      0b00000011
#define PCHAN               0
#define NCHAN               1
#define OFF                 0
#define ON                  1

#endif /* MODULES_DEFINES_ADS1299_H_ */
