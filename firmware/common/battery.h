/*
 * Battery monitoring -- board-dispatched.
 *
 * Each supported board wires an ADC pin to a voltage divider sampling VBAT.
 * The implementation picks the right pin + scaling based on compile-time
 * platform flags (NX_PLATFORM_ESP32 / NX_PLATFORM_NRF52 and the board
 * macros PlatformIO defines).
 *
 * Unsupported boards: battery_read_mv() returns -1 so callers can decide
 * whether to hide the UI or show "--".
 */
#ifndef NX_FIRMWARE_BATTERY_H
#define NX_FIRMWARE_BATTERY_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* One-time init (configure ADC attenuation / reference). */
void battery_init(void);

/* Millivolts at the battery terminal, or -1 if not supported. */
int32_t battery_read_mv(void);

/*
 * Rough state-of-charge based on a LiPo discharge curve
 * (3300 mV empty, 4200 mV full), clamped to 0..100.
 * Returns -1 if unsupported.
 */
int battery_percent(void);

#ifdef __cplusplus
}
#endif

#endif /* NX_FIRMWARE_BATTERY_H */
