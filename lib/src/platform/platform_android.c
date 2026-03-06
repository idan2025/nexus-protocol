/*
 * NEXUS Protocol -- Android Platform Implementation
 *
 * Build with Android NDK. Uses /dev/urandom, clock_gettime, pthreads.
 * Very similar to POSIX but avoids glibc-specific features.
 */
#ifdef __ANDROID__

#include "nexus/platform.h"

#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>

/* ── Random Bytes ────────────────────────────────────────────────────── */

nx_err_t nx_platform_random(uint8_t *buf, size_t len)
{
    if (!buf || len == 0) return NX_ERR_INVALID_ARG;

    int fd = open("/dev/urandom", O_RDONLY);
    if (fd < 0) return NX_ERR_IO;

    size_t total = 0;
    while (total < len) {
        ssize_t n = read(fd, buf + total, len - total);
        if (n <= 0) { close(fd); return NX_ERR_IO; }
        total += (size_t)n;
    }
    close(fd);
    return NX_OK;
}

/* ── Monotonic Time ──────────────────────────────────────────────────── */

uint64_t nx_platform_time_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
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

/* ── Mutex (pthreads) ────────────────────────────────────────────────── */

struct nx_mutex {
    pthread_mutex_t mtx;
};

nx_err_t nx_mutex_init(nx_mutex_t **mtx)
{
    if (!mtx) return NX_ERR_INVALID_ARG;

    *mtx = (nx_mutex_t *)malloc(sizeof(nx_mutex_t));
    if (!*mtx) return NX_ERR_NO_MEMORY;

    if (pthread_mutex_init(&(*mtx)->mtx, NULL) != 0) {
        free(*mtx);
        *mtx = NULL;
        return NX_ERR_IO;
    }
    return NX_OK;
}

nx_err_t nx_mutex_lock(nx_mutex_t *mtx)
{
    if (!mtx) return NX_ERR_INVALID_ARG;
    return (pthread_mutex_lock(&mtx->mtx) == 0) ? NX_OK : NX_ERR_IO;
}

nx_err_t nx_mutex_unlock(nx_mutex_t *mtx)
{
    if (!mtx) return NX_ERR_INVALID_ARG;
    return (pthread_mutex_unlock(&mtx->mtx) == 0) ? NX_OK : NX_ERR_IO;
}

void nx_mutex_destroy(nx_mutex_t *mtx)
{
    if (!mtx) return;
    pthread_mutex_destroy(&mtx->mtx);
    free(mtx);
}

#endif /* __ANDROID__ */
