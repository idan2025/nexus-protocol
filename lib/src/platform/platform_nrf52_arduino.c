/*
 * NEXUS Protocol -- nRF52840 Platform Implementation (Arduino/Adafruit BSP)
 *
 * Build with: -DNX_PLATFORM_NRF52 -DNX_NRF52_ARDUINO
 * For use with PlatformIO + Arduino framework (Adafruit nRF52 BSP).
 * The Zephyr version (platform_nrf52.c) is for Zephyr RTOS builds.
 */
#if defined(NX_PLATFORM_NRF52) && defined(ARDUINO)

#include "nexus/platform.h"
#include <stdlib.h>
#include <string.h>
#include <Arduino.h>

/* nRF52840 hardware RNG via Adafruit BSP */
#include <nrf_soc.h>

/* ── Random Bytes ────────────────────────────────────────────────────── */

nx_err_t nx_platform_random(uint8_t *buf, size_t len)
{
    if (!buf || len == 0) return NX_ERR_INVALID_ARG;

    /* Use nRF52840 hardware TRNG via SoftDevice or raw RNG */
    size_t pos = 0;
    while (pos < len) {
        uint8_t avail = 0;
        uint32_t val;

        /* Try SoftDevice RNG first */
        if (sd_rand_application_vector_get(buf + pos, len - pos) == NRF_SUCCESS) {
            break;
        }

        /* Fallback: use analogRead noise + micros() as seed */
        val = (uint32_t)(micros() ^ analogRead(0));
        size_t chunk = (len - pos < 4) ? (len - pos) : 4;
        memcpy(buf + pos, &val, chunk);
        pos += chunk;
    }
    return NX_OK;
}

/* ── Monotonic Time ──────────────────────────────────────────────────── */

uint64_t nx_platform_time_ms(void)
{
    return (uint64_t)millis();
}

/* ── Memory ──────────────────────────────────────────────────────────── */

void *nx_platform_alloc(size_t size)
{
    return malloc(size);
}

void nx_platform_free(void *ptr)
{
    free(ptr);
}

/* ── Mutex (FreeRTOS via Adafruit BSP) ───────────────────────────────── */

#include <FreeRTOS.h>
#include <semphr.h>

struct nx_mutex {
    SemaphoreHandle_t sem;
};

nx_err_t nx_mutex_init(nx_mutex_t **mtx)
{
    if (!mtx) return NX_ERR_INVALID_ARG;
    nx_mutex_t *m = (nx_mutex_t *)malloc(sizeof(nx_mutex_t));
    if (!m) return NX_ERR_NO_MEMORY;
    m->sem = xSemaphoreCreateMutex();
    if (!m->sem) { free(m); return NX_ERR_NO_MEMORY; }
    *mtx = m;
    return NX_OK;
}

nx_err_t nx_mutex_lock(nx_mutex_t *mtx)
{
    if (!mtx) return NX_ERR_INVALID_ARG;
    return (xSemaphoreTake(mtx->sem, portMAX_DELAY) == pdTRUE)
        ? NX_OK : NX_ERR_IO;
}

nx_err_t nx_mutex_unlock(nx_mutex_t *mtx)
{
    if (!mtx) return NX_ERR_INVALID_ARG;
    return (xSemaphoreGive(mtx->sem) == pdTRUE)
        ? NX_OK : NX_ERR_IO;
}

void nx_mutex_destroy(nx_mutex_t *mtx)
{
    if (!mtx) return;
    if (mtx->sem) vSemaphoreDelete(mtx->sem);
    free(mtx);
}

#endif /* NX_PLATFORM_NRF52 && ARDUINO */
