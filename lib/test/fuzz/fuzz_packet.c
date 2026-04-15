/*
 * Fuzz target: nx_packet_deserialize.
 *
 * Feeds arbitrary bytes into the wire-format packet parser and relies on
 * ASan + UBSan to catch out-of-bounds reads, sign overflow, etc.
 */

#include <stdint.h>
#include <stddef.h>

#include "nexus/packet.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    nx_packet_t pkt;
    (void)nx_packet_deserialize(data, size, &pkt);
    return 0;
}
