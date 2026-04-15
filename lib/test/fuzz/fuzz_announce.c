/*
 * Fuzz target: nx_announce_parse.
 *
 * Exercises the announce payload parser + signature verification path on
 * arbitrary input. The signature check will almost always reject, but the
 * parse path still touches every byte.
 */

#include <stdint.h>
#include <stddef.h>

#include "nexus/announce.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    nx_announce_t ann;
    (void)nx_announce_parse(data, size, &ann);
    return 0;
}
