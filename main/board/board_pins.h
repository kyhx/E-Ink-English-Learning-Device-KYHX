/*
 * board_pins.h — single source of truth for board GPIOs.
 *
 * Board: YD-ESP32-S3 / "ESP32-S3-N16R8" (ESP32-S3-WROOM-1, 16 MB flash,
 * 8 MB octal PSRAM), dual USB-C:
 *   - "COM" port = CH343P USB-UART (GPIO43/44, our console/flash port)
 *   - "USB" port = native USB (GPIO19/20)
 *   - WS2812 RGB LED = GPIO48 (only after bridging the RGB pads)
 *
 * Board-specific pin notes (from the user guide P1/P2 tables):
 *   - GPIO13 and GPIO14 ARE exposed on P1 pins 19/20.  They are ADC2
 *     channels, so use them as digital I/O only (ADC2 conflicts with Wi-Fi).
 *   - GPIO35/36/37 are used by the N16R8 flash/PSRAM bus; do not use.
 *   - GPIO19/20 are the native USB D-/D+; do not use.
 *   - GPIO43/44 are UART0 TX/RX (CH343P console); do not use.
 *   - GPIO0 is the BOOT button; GPIO45/46 are strapping pins.
 *   - GPIO48 is the WS2812 RGB LED (requires solder-bridge).
 *
 * Keep this file free of driver includes where possible; it is included by
 * board/services/app code.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef BOARD_PINS_H
#define BOARD_PINS_H

#include "driver/gpio.h"

/* ------------------------------------------------------------- E-Paper */
#define BOARD_EPD_BUSY_GPIO   GPIO_NUM_7
#define BOARD_EPD_RST_GPIO    GPIO_NUM_8
#define BOARD_EPD_DC_GPIO     GPIO_NUM_9
#define BOARD_EPD_CS_GPIO     GPIO_NUM_10
#define BOARD_EPD_MOSI_GPIO   GPIO_NUM_11
#define BOARD_EPD_SCK_GPIO    GPIO_NUM_12

/* --------------------------------------------------------------- Keys
 * Active low, internal pull-up.  All five pins are RTC GPIOs (0..21), so
 * every key can wake the board from deep sleep via EXT1.
 *
 *   PREV=4  NEXT=5  PLAY=6  RECORD=13  SYNC=1
 */
#define BOARD_KEY_PREV_GPIO   GPIO_NUM_4
#define BOARD_KEY_NEXT_GPIO   GPIO_NUM_5
#define BOARD_KEY_PLAY_GPIO   GPIO_NUM_6
#define BOARD_KEY_RECORD_GPIO GPIO_NUM_13
#define BOARD_KEY_SYNC_GPIO   GPIO_NUM_1

/* ---------------------------------------------------------- Battery ADC */
#define BOARD_BAT_ADC_GPIO    GPIO_NUM_2

/* --------------------------------------------------------- I2C (INA219) */
#define BOARD_I2C_SDA_GPIO    GPIO_NUM_39
#define BOARD_I2C_SCL_GPIO    GPIO_NUM_40

/* --------------------------------------------------------------- Audio
 * GPIO14 is exposed on P1 pin 20 (ADC2_CH3) and used here as I2S BCLK,
 * not as an ADC.  GPIO15/16 are on P1 pins 8/9.
 */
#define BOARD_MIC_BCLK_GPIO   GPIO_NUM_14
#define BOARD_MIC_WS_GPIO     GPIO_NUM_15
#define BOARD_MIC_SD_GPIO     GPIO_NUM_16
/* Optional MOS/load-switch gate on the MS4030 3.3 V rail.  GPIO38 is a free
 * P2 pin on the YD-ESP32-S3.  If the mic is wired directly to 3.3 V, set
 * CONFIG_APP_AUDIO_MIC_POWER_GPIO=-1; toggling an unused pin is harmless. */
#define BOARD_MIC_PWR_GPIO    GPIO_NUM_38
#define BOARD_AMP_BCLK_GPIO   GPIO_NUM_17
#define BOARD_AMP_LRC_GPIO    GPIO_NUM_18
#define BOARD_AMP_DIN_GPIO    GPIO_NUM_21
#define BOARD_AMP_SD_GPIO     GPIO_NUM_47

#endif /* BOARD_PINS_H */
