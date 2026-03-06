/*
 * NEXUS Protocol -- ESP32-S3 Platform Implementation (FreeRTOS)
 *
 * Build with: -DNX_PLATFORM_ESP32
 * This file is NOT compiled on desktop -- it requires ESP-IDF headers.
 */
#ifdef NX_PLATFORM_ESP32

#include "nexus/platform.h"

#include "esp_random.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include <string.h>
#include <stdlib.h>

/* ── Random Bytes ────────────────────────────────────────────────────── */

nx_err_t nx_platform_random(uint8_t *buf, size_t len)
{
    if (!buf || len == 0) return NX_ERR_INVALID_ARG;

    /* esp_fill_random uses hardware RNG (RF subsystem noise) */
    esp_fill_random(buf, len);
    return NX_OK;
}

/* ── Monotonic Time ──────────────────────────────────────────────────── */

uint64_t nx_platform_time_ms(void)
{
    /* xTaskGetTickCount returns ticks since scheduler started */
    return (uint64_t)xTaskGetTickCount() * portTICK_PERIOD_MS;
}

/* ── Memory ──────────────────────────────────────────────────────────── */

void *nx_platform_alloc(size_t size)
{
    return pvPortMalloc(size);
}

void nx_platform_free(void *ptr)
{
    vPortFree(ptr);
}

/* ── Mutex (FreeRTOS) ────────────────────────────────────────────────── */

struct nx_mutex {
    SemaphoreHandle_t sem;
};

nx_err_t nx_mutex_init(nx_mutex_t **mtx)
{
    if (!mtx) return NX_ERR_INVALID_ARG;

    *mtx = (nx_mutex_t *)pvPortMalloc(sizeof(nx_mutex_t));
    if (!*mtx) return NX_ERR_NO_MEMORY;

    (*mtx)->sem = xSemaphoreCreateMutex();
    if (!(*mtx)->sem) {
        vPortFree(*mtx);
        *mtx = NULL;
        return NX_ERR_NO_MEMORY;
    }
    return NX_OK;
}

nx_err_t nx_mutex_lock(nx_mutex_t *mtx)
{
    if (!mtx || !mtx->sem) return NX_ERR_INVALID_ARG;
    return (xSemaphoreTake(mtx->sem, portMAX_DELAY) == pdTRUE)
        ? NX_OK : NX_ERR_TIMEOUT;
}

nx_err_t nx_mutex_unlock(nx_mutex_t *mtx)
{
    if (!mtx || !mtx->sem) return NX_ERR_INVALID_ARG;
    return (xSemaphoreGive(mtx->sem) == pdTRUE)
        ? NX_OK : NX_ERR_IO;
}

void nx_mutex_destroy(nx_mutex_t *mtx)
{
    if (!mtx) return;
    if (mtx->sem) vSemaphoreDelete(mtx->sem);
    vPortFree(mtx);
}

#endif /* NX_PLATFORM_ESP32 */
