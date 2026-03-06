/*
 * NEXUS Protocol -- Serial/UART Transport
 *
 * Simple framing protocol:
 *   [0x7E] [LEN_HI] [LEN_LO] [PAYLOAD...] [0x7E]
 * LEN is big-endian 16-bit payload length.
 * No byte stuffing needed since payloads are encrypted (random-looking).
 */
#define _DEFAULT_SOURCE

#include "nexus/transport.h"
#include "nexus/platform.h"

#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <errno.h>
#include <poll.h>

#define SERIAL_FRAME_DELIM  0x7E
#define SERIAL_MAX_FRAME    (NX_MAX_PACKET + 4) /* delim + 2 len + payload + delim */

typedef struct {
    int     fd;
    uint8_t rx_buf[SERIAL_MAX_FRAME];
    size_t  rx_pos;
} serial_state_t;

static speed_t baud_to_speed(uint32_t baud)
{
    switch (baud) {
        case 9600:   return B9600;
        case 19200:  return B19200;
        case 38400:  return B38400;
        case 57600:  return B57600;
        case 115200: return B115200;
        case 230400: return B230400;
        default:     return B115200;
    }
}

static nx_err_t serial_init(nx_transport_t *t, const void *config)
{
    const nx_serial_config_t *cfg = (const nx_serial_config_t *)config;
    if (!cfg || !cfg->device) return NX_ERR_INVALID_ARG;

    serial_state_t *s = (serial_state_t *)nx_platform_alloc(sizeof(serial_state_t));
    if (!s) return NX_ERR_NO_MEMORY;

    memset(s, 0, sizeof(*s));

    s->fd = open(cfg->device, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (s->fd < 0) {
        nx_platform_free(s);
        return NX_ERR_IO;
    }

    /* Configure terminal for raw mode */
    struct termios tty;
    if (tcgetattr(s->fd, &tty) != 0) {
        close(s->fd);
        nx_platform_free(s);
        return NX_ERR_IO;
    }

    cfmakeraw(&tty);
    speed_t spd = baud_to_speed(cfg->baud_rate);
    cfsetispeed(&tty, spd);
    cfsetospeed(&tty, spd);

    tty.c_cflag |= (CLOCAL | CREAD);
    tty.c_cc[VMIN]  = 0;
    tty.c_cc[VTIME] = 0;

    if (tcsetattr(s->fd, TCSANOW, &tty) != 0) {
        close(s->fd);
        nx_platform_free(s);
        return NX_ERR_IO;
    }

    t->state  = s;
    t->active = true;
    return NX_OK;
}

static nx_err_t serial_send(nx_transport_t *t, const uint8_t *data, size_t len)
{
    serial_state_t *s = (serial_state_t *)t->state;
    if (!s || s->fd < 0) return NX_ERR_TRANSPORT;
    if (len > NX_MAX_PACKET) return NX_ERR_INVALID_ARG;

    uint8_t frame[SERIAL_MAX_FRAME];
    size_t pos = 0;

    frame[pos++] = SERIAL_FRAME_DELIM;
    frame[pos++] = (uint8_t)(len >> 8);
    frame[pos++] = (uint8_t)(len);
    memcpy(&frame[pos], data, len);
    pos += len;
    frame[pos++] = SERIAL_FRAME_DELIM;

    size_t written = 0;
    while (written < pos) {
        ssize_t n = write(s->fd, frame + written, pos - written);
        if (n < 0) {
            if (errno == EINTR || errno == EAGAIN) continue;
            return NX_ERR_IO;
        }
        written += (size_t)n;
    }

    return NX_OK;
}

static nx_err_t serial_recv(nx_transport_t *t, uint8_t *buf, size_t buf_len,
                            size_t *out_len, uint32_t timeout_ms)
{
    serial_state_t *s = (serial_state_t *)t->state;
    if (!s || s->fd < 0) return NX_ERR_TRANSPORT;

    uint64_t deadline = nx_platform_time_ms() + timeout_ms;

    /* State machine: find start delimiter, read length, read payload, find end */
    enum { WAIT_START, READ_LEN_HI, READ_LEN_LO, READ_PAYLOAD, WAIT_END } state = WAIT_START;
    uint16_t frame_len = 0;
    size_t   payload_pos = 0;

    for (;;) {
        /* Check timeout */
        uint64_t now = nx_platform_time_ms();
        if (now >= deadline) return NX_ERR_TIMEOUT;

        int remaining = (int)(deadline - now);
        struct pollfd pfd = { .fd = s->fd, .events = POLLIN };
        int pr = poll(&pfd, 1, remaining);
        if (pr < 0) {
            if (errno == EINTR) continue;
            return NX_ERR_IO;
        }
        if (pr == 0) return NX_ERR_TIMEOUT;

        uint8_t byte;
        ssize_t n = read(s->fd, &byte, 1);
        if (n < 0) {
            if (errno == EINTR || errno == EAGAIN) continue;
            return NX_ERR_IO;
        }
        if (n == 0) continue;

        switch (state) {
        case WAIT_START:
            if (byte == SERIAL_FRAME_DELIM) state = READ_LEN_HI;
            break;
        case READ_LEN_HI:
            frame_len = (uint16_t)((uint16_t)byte << 8);
            state = READ_LEN_LO;
            break;
        case READ_LEN_LO:
            frame_len |= byte;
            if (frame_len == 0 || frame_len > NX_MAX_PACKET) {
                state = WAIT_START; /* Invalid, reset */
                break;
            }
            if (frame_len > buf_len) return NX_ERR_BUFFER_TOO_SMALL;
            payload_pos = 0;
            state = READ_PAYLOAD;
            break;
        case READ_PAYLOAD:
            buf[payload_pos++] = byte;
            if (payload_pos >= frame_len) state = WAIT_END;
            break;
        case WAIT_END:
            if (byte == SERIAL_FRAME_DELIM) {
                *out_len = frame_len;
                return NX_OK;
            }
            /* Missing end delimiter -- discard and restart */
            state = WAIT_START;
            break;
        }
    }
}

static void serial_destroy(nx_transport_t *t)
{
    serial_state_t *s = (serial_state_t *)t->state;
    if (s) {
        if (s->fd >= 0) close(s->fd);
        nx_platform_free(s);
    }
    t->state  = NULL;
    t->active = false;
}

static const nx_transport_ops_t serial_ops = {
    .init    = serial_init,
    .send    = serial_send,
    .recv    = serial_recv,
    .destroy = serial_destroy,
};

nx_transport_t *nx_serial_transport_create(void)
{
    nx_transport_t *t = (nx_transport_t *)nx_platform_alloc(sizeof(nx_transport_t));
    if (!t) return NULL;

    memset(t, 0, sizeof(*t));
    t->type   = NX_TRANSPORT_SERIAL;
    t->name   = "serial";
    t->ops    = &serial_ops;
    t->active = false;
    return t;
}
