/*
 * Seeed XIAO nRF52840 variant for Adafruit nRF52 BSP
 *
 * Pin mapping from Seeed schematic and Meshtastic firmware reference.
 * XIAO nRF52840 pinout:
 *   D0=P0.02  D1=P0.03  D2=P0.28  D3=P0.29  D4=P0.04  D5=P0.05
 *   D6=P1.11  D7=P1.12  D8=P1.13  D9=P1.14  D10=P1.15
 */
#ifndef _VARIANT_SEEED_XIAO_NRF52840_
#define _VARIANT_SEEED_XIAO_NRF52840_

#define VARIANT_MCK (64000000ul)

#define USE_LFXO

#include "WVariant.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PINS_COUNT    (33)
#define NUM_DIGITAL_PINS (33)
#define NUM_ANALOG_INPUTS (6)
#define NUM_ANALOG_OUTPUTS (0)

/* Digital pins */
#define D0   (0ul)
#define D1   (1ul)
#define D2   (2ul)
#define D3   (3ul)
#define D4   (4ul)
#define D5   (5ul)
#define D6   (6ul)
#define D7   (7ul)
#define D8   (8ul)
#define D9   (9ul)
#define D10  (10ul)

/* LEDs (active LOW on XIAO nRF52840) */
#define PIN_LED1       (11)  /* Red LED */
#define PIN_LED2       (12)  /* Blue LED */
#define PIN_LED3       (13)  /* Green LED */

#define LED_BUILTIN    PIN_LED1
#define LED_RED        PIN_LED1
#define LED_BLUE       PIN_LED2
#define LED_GREEN      PIN_LED3

#define LED_STATE_ON   0  /* Active LOW */

/* Analog pins */
#define PIN_A0   (0)
#define PIN_A1   (1)
#define PIN_A2   (2)
#define PIN_A3   (3)
#define PIN_A4   (4)
#define PIN_A5   (5)

static const uint8_t A0 = PIN_A0;
static const uint8_t A1 = PIN_A1;
static const uint8_t A2 = PIN_A2;
static const uint8_t A3 = PIN_A3;
static const uint8_t A4 = PIN_A4;
static const uint8_t A5 = PIN_A5;
#define ADC_RESOLUTION 14

/* Other pins */
#define PIN_AREF  (2)
#define PIN_NFC1  (9)
#define PIN_NFC2  (10)
#define PIN_VBAT  (32)  /* P0.31 via voltage divider */

static const uint8_t AREF = PIN_AREF;

/* Serial interfaces */
#define PIN_SERIAL1_RX (7)   /* D7 */
#define PIN_SERIAL1_TX (6)   /* D6 */
#define PIN_SERIAL2_RX (-1)
#define PIN_SERIAL2_TX (-1)

/* SPI */
#define SPI_INTERFACES_COUNT 1

#define PIN_SPI_MISO  (9)   /* D9 */
#define PIN_SPI_MOSI  (10)  /* D10 */
#define PIN_SPI_SCK   (8)   /* D8 */

static const uint8_t SS   = (4);  /* D4 (SX1262 CS for WIO-SX1262) */
static const uint8_t MOSI = PIN_SPI_MOSI;
static const uint8_t MISO = PIN_SPI_MISO;
static const uint8_t SCK  = PIN_SPI_SCK;

/* I2C */
#define WIRE_INTERFACES_COUNT 1

#define PIN_WIRE_SDA  (4)   /* D4 */
#define PIN_WIRE_SCL  (5)   /* D5 */

#ifdef __cplusplus
}
#endif

#endif /* _VARIANT_SEEED_XIAO_NRF52840_ */
