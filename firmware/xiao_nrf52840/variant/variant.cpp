/*
 * Seeed XIAO nRF52840 variant -- pin mapping table
 *
 * Maps Arduino pin numbers to nRF52840 GPIO numbers.
 * Reference: Seeed XIAO nRF52840 schematic + Meshtastic firmware.
 */
#include "variant.h"
#include "wiring_constants.h"
#include "wiring_digital.h"
#include "nrf.h"

const uint32_t g_ADigitalPinMap[] =
{
    // D0 .. D10 (XIAO header pins)
     2,  // D0  is P0.02
     3,  // D1  is P0.03
    28,  // D2  is P0.28
    29,  // D3  is P0.29
     4,  // D4  is P0.04
     5,  // D5  is P0.05
    43,  // D6  is P1.11
    44,  // D7  is P1.12
    45,  // D8  is P1.13 (SPI SCK)
    46,  // D9  is P1.14 (SPI MISO)
    47,  // D10 is P1.15 (SPI MOSI)

    // D11 .. D13 (LEDs, active LOW)
    26,  // D11 is P0.26 (LED RED)
     6,  // D12 is P0.06 (LED BLUE)
    30,  // D13 is P0.30 (LED GREEN)

    // D14 .. D15 (I2C1, internal)
    24,  // D14 is P0.24 (I2C1 SCL -- LSM6DS3 on Sense model)
    25,  // D15 is P0.25 (I2C1 SDA -- LSM6DS3 on Sense model)

    // D16 .. D17 (internal)
    16,  // D16 is P0.16 (LSM6DS3 INT / unused on base model)
    14,  // D17 is P0.14 (VBAT_ENABLE)

    // D18 .. D19 (NFC pins, usable as GPIO)
     9,  // D18 is P0.09 (NFC1)
    10,  // D19 is P0.10 (NFC2)

    // D20 .. D21 (USB)
    13,  // D20 is P0.13 (charge rate / HICHG)
    15,  // D21 is P0.15

    // D22 .. D23 (more internal)
    17,  // D22 is P0.17 (charge status)
    11,  // D23 is P0.11

    // D24 .. D31 (pad out to match PINS_COUNT)
    12,  // D24 is P0.12
     8,  // D25 is P0.08
     7,  // D26 is P0.07
    27,  // D27 is P0.27
    31,  // D28 is P0.31 (VBAT analog)
    20,  // D29 is P0.20
    22,  // D30 is P0.22
    23,  // D31 is P0.23
    21,  // D32 is P0.21
};

void initVariant()
{
    /* LEDs off (active LOW) */
    pinMode(PIN_LED1, OUTPUT);
    digitalWrite(PIN_LED1, HIGH);

    pinMode(PIN_LED2, OUTPUT);
    digitalWrite(PIN_LED2, HIGH);

    pinMode(PIN_LED3, OUTPUT);
    digitalWrite(PIN_LED3, HIGH);
}
