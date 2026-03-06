/*
 * NEXUS Protocol -- nRF52840 Platform Implementation (Zephyr RTOS)
 *
 * Build with: -DNX_PLATFORM_NRF52
 * This file is NOT compiled on desktop -- it requires Zephyr headers.
 *
 * Target boards:
 *   - RAK4631 (nRF52840 + SX1262)
 *   - Any nRF52840-based board running Zephyr
 *
 * Memory budget: ~40KB of 256KB SRAM
 */
#ifdef NX_PLATFORM_NRF52

#include "nexus/platform.h"

#include <zephyr/kernel.h>
#include <zephyr/random/random.h>
#include <zephyr/sys/util.h>

#include <string.h>

/* ── Random Bytes ────────────────────────────────────────────────────── */

nx_err_t nx_platform_random(uint8_t *buf, size_t len)
{
    if (!buf || len == 0) return NX_ERR_INVALID_ARG;

    /* Zephyr's sys_rand_get uses the nRF52840 hardware TRNG */
    sys_rand_get(buf, len);
    return NX_OK;
}

/* ── Monotonic Time ──────────────────────────────────────────────────── */

uint64_t nx_platform_time_ms(void)
{
    return (uint64_t)k_uptime_get();
}

/* ── Memory ──────────────────────────────────────────────────────────── */

/* Use static pool to avoid heap fragmentation on constrained MCU.
 * 16KB pool should be sufficient for routing tables + buffers. */
K_HEAP_DEFINE(nx_heap, 16384);

void *nx_platform_alloc(size_t size)
{
    return k_heap_alloc(&nx_heap, size, K_NO_WAIT);
}

void nx_platform_free(void *ptr)
{
    if (ptr) k_heap_free(&nx_heap, ptr);
}

/* ── Mutex (Zephyr) ──────────────────────────────────────────────────── */

struct nx_mutex {
    struct k_mutex mtx;
};

/* Static mutex pool (avoid dynamic alloc for mutexes) */
#define NX_MAX_MUTEXES 4
static struct nx_mutex mutex_pool[NX_MAX_MUTEXES];
static bool mutex_used[NX_MAX_MUTEXES];

nx_err_t nx_mutex_init(nx_mutex_t **mtx)
{
    if (!mtx) return NX_ERR_INVALID_ARG;

    for (int i = 0; i < NX_MAX_MUTEXES; i++) {
        if (!mutex_used[i]) {
            mutex_used[i] = true;
            k_mutex_init(&mutex_pool[i].mtx);
            *mtx = &mutex_pool[i];
            return NX_OK;
        }
    }
    return NX_ERR_NO_MEMORY;
}

nx_err_t nx_mutex_lock(nx_mutex_t *mtx)
{
    if (!mtx) return NX_ERR_INVALID_ARG;
    return (k_mutex_lock(&mtx->mtx, K_FOREVER) == 0)
        ? NX_OK : NX_ERR_IO;
}

nx_err_t nx_mutex_unlock(nx_mutex_t *mtx)
{
    if (!mtx) return NX_ERR_INVALID_ARG;
    return (k_mutex_unlock(&mtx->mtx) == 0)
        ? NX_OK : NX_ERR_IO;
}

void nx_mutex_destroy(nx_mutex_t *mtx)
{
    if (!mtx) return;
    for (int i = 0; i < NX_MAX_MUTEXES; i++) {
        if (&mutex_pool[i] == mtx) {
            mutex_used[i] = false;
            break;
        }
    }
}

#endif /* NX_PLATFORM_NRF52 */
