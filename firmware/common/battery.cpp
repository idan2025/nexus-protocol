#include "battery.h"
#include <Arduino.h>

/*
 * Per-board pin + scaling selection.
 *
 * ESP32-S3 Heltec V3:  VBAT on GPIO1 via ~390k/100k divider (factor 4.9),
 *                      VBAT_CTRL (GPIO37) must be pulled LOW to enable.
 * XIAO ESP32-S3:       no internal battery divider exposed on the module;
 *                      boards can wire VBAT -> A0 externally if desired.
 * RAK4631 nRF52840:    VBAT_MV on PIN_VBAT (P0_04), divider factor 2.
 * XIAO nRF52840:       P0_31 reads VBAT via 1M/510k divider (factor 2.96),
 *                      enable with READ_BAT_ENABLE (P0_14) LOW.
 */

#if defined(ARDUINO_heltec_wifi_lora_32_V3)
  #define BAT_ADC_PIN     1
  #define BAT_CTRL_PIN    37
  #define BAT_FACTOR_X10  49  /* × 4.9 */
  #define BAT_REF_MV      3300
  #define BAT_ADC_MAX     4095
  #define BAT_SUPPORTED   1

#elif defined(ARDUINO_WIO_WM1110) || defined(ARDUINO_RAK4631_NRF52840) || \
      defined(WB_A0) || defined(ARDUINO_WisBlock_RAK4631)
  #ifdef PIN_VBAT
    #define BAT_ADC_PIN     PIN_VBAT
  #else
    #define BAT_ADC_PIN     5  /* P0_05 on most RAK4631 variants */
  #endif
  #define BAT_FACTOR_X10  20   /* × 2.0 */
  #define BAT_REF_MV      3600 /* nRF52 internal VDD reference */
  #define BAT_ADC_MAX     4095
  #define BAT_SUPPORTED   1

#elif defined(ARDUINO_Seeed_XIAO_nRF52840) || defined(ARDUINO_SEEED_XIAO_NRF52840)
  #define BAT_ADC_PIN     31   /* P0_31 */
  #define BAT_ENABLE_PIN  14   /* P0_14, drive LOW to enable divider */
  #define BAT_FACTOR_X10  30   /* ~× 2.96, rounded */
  #define BAT_REF_MV      3600
  #define BAT_ADC_MAX     4095
  #define BAT_SUPPORTED   1

#else
  #define BAT_SUPPORTED   0
#endif

void battery_init(void)
{
#if BAT_SUPPORTED
  #ifdef BAT_CTRL_PIN
    pinMode(BAT_CTRL_PIN, OUTPUT);
    digitalWrite(BAT_CTRL_PIN, LOW);
  #endif
  #ifdef BAT_ENABLE_PIN
    pinMode(BAT_ENABLE_PIN, OUTPUT);
    digitalWrite(BAT_ENABLE_PIN, LOW);
  #endif
  #if defined(NX_PLATFORM_ESP32)
    analogReadResolution(12);
    /* 11dB ~= up to 3.3V input range on ESP32-S3. */
    analogSetPinAttenuation(BAT_ADC_PIN, ADC_11db);
  #elif defined(NX_PLATFORM_NRF52)
    analogReadResolution(12);
    analogReference(AR_INTERNAL_3_6);
  #endif
#endif
}

int32_t battery_read_mv(void)
{
#if BAT_SUPPORTED
    int raw = analogRead(BAT_ADC_PIN);
    if (raw < 0) return -1;
    /* mv_at_pin = raw * Vref / adc_max */
    int32_t mv_at_pin = ((int32_t)raw * BAT_REF_MV) / BAT_ADC_MAX;
    /* Undo the voltage-divider scaling. */
    int32_t vbat_mv = (mv_at_pin * BAT_FACTOR_X10) / 10;
    return vbat_mv;
#else
    return -1;
#endif
}

int battery_percent(void)
{
    int32_t mv = battery_read_mv();
    if (mv < 0) return -1;
    /* LiPo linear approximation 3300-4200 mV. */
    const int32_t LOW_MV = 3300;
    const int32_t HIGH_MV = 4200;
    if (mv <= LOW_MV) return 0;
    if (mv >= HIGH_MV) return 100;
    return (int)(((mv - LOW_MV) * 100) / (HIGH_MV - LOW_MV));
}
