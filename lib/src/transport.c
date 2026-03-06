/*
 * NEXUS Protocol -- Transport Interface & Registry
 */
#include "nexus/transport.h"

#include <string.h>

static nx_transport_t *registry[NX_MAX_TRANSPORTS];
static int registry_count = 0;

void nx_transport_registry_init(void)
{
    memset(registry, 0, sizeof(registry));
    registry_count = 0;
}

nx_err_t nx_transport_register(nx_transport_t *t)
{
    if (!t || !t->ops) return NX_ERR_INVALID_ARG;
    if (registry_count >= NX_MAX_TRANSPORTS) return NX_ERR_FULL;

    registry[registry_count++] = t;
    return NX_OK;
}

int nx_transport_count(void)
{
    return registry_count;
}

nx_transport_t *nx_transport_get(int index)
{
    if (index < 0 || index >= registry_count) return NULL;
    return registry[index];
}

nx_err_t nx_transport_send(nx_transport_t *t, const uint8_t *data, size_t len)
{
    if (!t || !t->ops || !t->ops->send) return NX_ERR_INVALID_ARG;
    if (!t->active) return NX_ERR_TRANSPORT;
    return t->ops->send(t, data, len);
}

nx_err_t nx_transport_recv(nx_transport_t *t, uint8_t *buf, size_t buf_len,
                           size_t *out_len, uint32_t timeout_ms)
{
    if (!t || !t->ops || !t->ops->recv) return NX_ERR_INVALID_ARG;
    if (!t->active) return NX_ERR_TRANSPORT;
    return t->ops->recv(t, buf, buf_len, out_len, timeout_ms);
}

void nx_transport_set_active(nx_transport_t *t, bool active)
{
    if (t) t->active = active;
}

void nx_transport_destroy(nx_transport_t *t)
{
    if (!t) return;
    if (t->ops && t->ops->destroy) {
        t->ops->destroy(t);
    }
}
