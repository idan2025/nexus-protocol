/*
 * Fuzz target: nx_msg_parse + common accessors.
 *
 * Parses NXM envelopes and, on success, exercises the zero-copy field
 * accessors so corrupt length prefixes surface as OOB reads under ASan.
 */

#include <stdint.h>
#include <stddef.h>

#include "nexus/message.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    nx_message_t msg;
    if (nx_msg_parse(data, size, &msg) != NX_OK) {
        return 0;
    }

    size_t text_len = 0;
    (void)nx_msg_get_text(&msg, &text_len);

    double lat = 0, lon = 0;
    int16_t alt = 0;
    uint8_t acc = 0;
    (void)nx_msg_get_location(&msg, &lat, &lon, &alt, &acc);

    nx_msg_id_t id;
    (void)nx_msg_get_msg_id(&msg, &id);

    return 0;
}
