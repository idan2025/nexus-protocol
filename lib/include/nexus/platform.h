/*
 * NEXUS Protocol -- Platform Abstraction Interface
 */
#ifndef NEXUS_PLATFORM_H
#define NEXUS_PLATFORM_H

#include "types.h"

/* ── Random Bytes ────────────────────────────────────────────────────── */
nx_err_t nx_platform_random(uint8_t *buf, size_t len);

/* ── Monotonic Time (milliseconds) ───────────────────────────────────── */
uint64_t nx_platform_time_ms(void);

/* ── Memory ──────────────────────────────────────────────────────────── */
void *nx_platform_alloc(size_t size);
void  nx_platform_free(void *ptr);

/* ── Mutex (optional, no-op on bare-metal) ───────────────────────────── */
typedef struct nx_mutex nx_mutex_t;

nx_err_t nx_mutex_init(nx_mutex_t **mtx);
nx_err_t nx_mutex_lock(nx_mutex_t *mtx);
nx_err_t nx_mutex_unlock(nx_mutex_t *mtx);
void     nx_mutex_destroy(nx_mutex_t *mtx);

#endif /* NEXUS_PLATFORM_H */
