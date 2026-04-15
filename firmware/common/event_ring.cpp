#include "event_ring.h"
#include <Arduino.h>
#include <string.h>

static nx_event_t s_ring[NX_EVENT_RING_SIZE];
static size_t     s_head = 0;   /* next write position */
static size_t     s_count = 0;  /* number of valid entries */

void nx_event_log(const char *text)
{
    if (!text) return;
    nx_event_t *slot = &s_ring[s_head];
    slot->timestamp_s = (uint32_t)(millis() / 1000);
    strncpy(slot->text, text, NX_EVENT_TEXT_MAX - 1);
    slot->text[NX_EVENT_TEXT_MAX - 1] = '\0';

    s_head = (s_head + 1) % NX_EVENT_RING_SIZE;
    if (s_count < NX_EVENT_RING_SIZE) s_count++;
}

size_t nx_event_recent(nx_event_t *out, size_t max)
{
    if (!out || max == 0 || s_count == 0) return 0;
    size_t n = (max < s_count) ? max : s_count;
    /* Walk backwards from head. */
    size_t idx = (s_head == 0) ? NX_EVENT_RING_SIZE - 1 : s_head - 1;
    for (size_t i = 0; i < n; i++) {
        out[i] = s_ring[idx];
        idx = (idx == 0) ? NX_EVENT_RING_SIZE - 1 : idx - 1;
    }
    return n;
}

size_t nx_event_count(void)
{
    return s_count;
}

void nx_event_reset(void)
{
    s_head = 0;
    s_count = 0;
}
