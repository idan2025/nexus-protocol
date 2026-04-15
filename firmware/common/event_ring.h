/*
 * Small, fixed-size event ring buffer shared across all firmware boards.
 *
 * Holds the most recent NX_EVENT_RING_SIZE events (announces seen,
 * messages received, transport errors). Wraps on overflow so lookup
 * is always O(1) and memory is bounded.
 *
 * Every event is a timestamp + short text (<= NX_EVENT_TEXT_MAX-1 chars,
 * NUL-terminated). Intended for display on an OLED or dump over serial.
 */
#ifndef NX_FIRMWARE_EVENT_RING_H
#define NX_FIRMWARE_EVENT_RING_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef NX_EVENT_RING_SIZE
#define NX_EVENT_RING_SIZE   16
#endif

#ifndef NX_EVENT_TEXT_MAX
#define NX_EVENT_TEXT_MAX    48
#endif

typedef struct {
    uint32_t timestamp_s;              /* Seconds since boot */
    char     text[NX_EVENT_TEXT_MAX];  /* NUL-terminated */
} nx_event_t;

/* Append an event. Oldest entry is overwritten when full. */
void nx_event_log(const char *text);

/*
 * Copy up to `max` most-recent events into `out`, newest first.
 * Returns number of events copied.
 */
size_t nx_event_recent(nx_event_t *out, size_t max);

/* Number of events currently stored (0..NX_EVENT_RING_SIZE). */
size_t nx_event_count(void);

/* Wipe the ring buffer. */
void nx_event_reset(void);

#ifdef __cplusplus
}
#endif

#endif /* NX_FIRMWARE_EVENT_RING_H */
