/*
 * NEXUS Protocol -- Message Fragmentation & Reassembly
 */
#include "nexus/fragment.h"
#include "nexus/packet.h"
#include "nexus/identity.h"
#include "nexus/platform.h"

#include <string.h>

/* ── Init / Expire ───────────────────────────────────────────────────── */

void nx_frag_init(nx_frag_buffer_t *fb)
{
    if (!fb) return;
    memset(fb, 0, sizeof(*fb));
}

void nx_frag_expire(nx_frag_buffer_t *fb, uint64_t now_ms)
{
    if (!fb) return;
    for (int i = 0; i < NX_FRAG_REASSEMBLY_SLOTS; i++) {
        if (fb->slots[i].valid &&
            now_ms - fb->slots[i].started_ms > NX_FRAG_TIMEOUT_MS) {
            fb->slots[i].valid = false;
        }
    }
}

/* ── Extended Header Encode/Decode ───────────────────────────────────── */

int nx_frag_encode_exthdr(const nx_frag_header_t *fh,
                          uint8_t *buf, size_t buf_len)
{
    if (!fh || !buf || buf_len < NX_FRAG_EXTHDR_SIZE) return NX_ERR_BUFFER_TOO_SMALL;

    buf[0] = NX_EXTHDR_FRAGMENT;
    buf[1] = (uint8_t)(fh->frag_id >> 8);
    buf[2] = (uint8_t)(fh->frag_id);
    buf[3] = (uint8_t)((fh->frag_index & 0x0F) << 4 |
                        ((fh->frag_total - 1) & 0x0F));
    return NX_FRAG_EXTHDR_SIZE;
}

nx_err_t nx_frag_decode_exthdr(const uint8_t *buf, size_t buf_len,
                               nx_frag_header_t *fh)
{
    if (!buf || !fh || buf_len < NX_FRAG_EXTHDR_SIZE) return NX_ERR_BUFFER_TOO_SMALL;
    if (buf[0] != NX_EXTHDR_FRAGMENT) return NX_ERR_INVALID_ARG;

    fh->frag_id    = (uint16_t)((uint16_t)buf[1] << 8 | (uint16_t)buf[2]);
    fh->frag_index = (buf[3] >> 4) & 0x0F;
    fh->frag_total = (buf[3] & 0x0F) + 1;
    return NX_OK;
}

/* ── Split ───────────────────────────────────────────────────────────── */

nx_err_t nx_frag_split(nx_frag_buffer_t *fb,
                       const nx_header_t *base_hdr,
                       const uint8_t *data, size_t data_len,
                       nx_packet_t *out_pkts, int *out_count)
{
    if (!fb || !base_hdr || !data || !out_pkts || !out_count)
        return NX_ERR_INVALID_ARG;

    /* Single packet -- no fragmentation needed */
    if (data_len <= NX_MAX_PAYLOAD) {
        memset(&out_pkts[0], 0, sizeof(out_pkts[0]));
        out_pkts[0].header = *base_hdr;
        out_pkts[0].header.flags = nx_packet_flags(
            false, false,
            nx_packet_flag_prio(base_hdr->flags),
            nx_packet_flag_ptype(base_hdr->flags),
            nx_packet_flag_rtype(base_hdr->flags));
        out_pkts[0].header.payload_len = (uint8_t)data_len;
        memcpy(out_pkts[0].payload, data, data_len);
        *out_count = 1;
        return NX_OK;
    }

    if (data_len > NX_FRAG_MAX_MESSAGE)
        return NX_ERR_BUFFER_TOO_SMALL;

    /* Calculate fragment count */
    int frag_count = (int)((data_len + NX_FRAG_PAYLOAD_CAP - 1) / NX_FRAG_PAYLOAD_CAP);
    if (frag_count > NX_FRAG_MAX_COUNT)
        return NX_ERR_BUFFER_TOO_SMALL;

    uint16_t frag_id = fb->next_frag_id++;
    size_t offset = 0;

    for (int i = 0; i < frag_count; i++) {
        size_t chunk = data_len - offset;
        if (chunk > NX_FRAG_PAYLOAD_CAP) chunk = NX_FRAG_PAYLOAD_CAP;

        bool is_last = (i == frag_count - 1);

        memset(&out_pkts[i], 0, sizeof(out_pkts[i]));
        out_pkts[i].header = *base_hdr;
        out_pkts[i].header.flags = nx_packet_flags(
            !is_last, true, /* FRAG set on all but last; EXTHDR always set */
            nx_packet_flag_prio(base_hdr->flags),
            nx_packet_flag_ptype(base_hdr->flags),
            nx_packet_flag_rtype(base_hdr->flags));

        /* Write extended header into payload */
        nx_frag_header_t fh = {
            .frag_id    = frag_id,
            .frag_index = (uint8_t)i,
            .frag_total = (uint8_t)frag_count,
        };
        nx_frag_encode_exthdr(&fh, out_pkts[i].payload, sizeof(out_pkts[i].payload));

        /* Copy data chunk after extended header */
        memcpy(out_pkts[i].payload + NX_FRAG_EXTHDR_SIZE, data + offset, chunk);
        out_pkts[i].header.payload_len = (uint8_t)(NX_FRAG_EXTHDR_SIZE + chunk);

        offset += chunk;
    }

    *out_count = frag_count;
    return NX_OK;
}

/* ── Receive / Reassemble ────────────────────────────────────────────── */

nx_err_t nx_frag_receive(nx_frag_buffer_t *fb,
                         const nx_packet_t *pkt,
                         uint8_t *out_data, size_t out_buf_len,
                         size_t *out_len, uint64_t now_ms)
{
    if (!fb || !pkt || !out_data || !out_len) return NX_ERR_INVALID_ARG;

    *out_len = 0;

    /* Decode extended header */
    if (pkt->header.payload_len < NX_FRAG_EXTHDR_SIZE) return NX_ERR_INVALID_ARG;

    nx_frag_header_t fh;
    nx_err_t err = nx_frag_decode_exthdr(pkt->payload, pkt->header.payload_len, &fh);
    if (err != NX_OK) return err;

    if (fh.frag_index >= fh.frag_total || fh.frag_total > NX_FRAG_MAX_COUNT)
        return NX_ERR_INVALID_ARG;

    size_t data_in_frag = pkt->header.payload_len - NX_FRAG_EXTHDR_SIZE;

    /* Find existing reassembly slot or allocate a new one */
    nx_reassembly_t *slot = NULL;
    int free_idx = -1;

    for (int i = 0; i < NX_FRAG_REASSEMBLY_SLOTS; i++) {
        if (fb->slots[i].valid) {
            if (nx_addr_short_cmp(&fb->slots[i].src, &pkt->header.src) == 0 &&
                fb->slots[i].frag_id == fh.frag_id) {
                slot = &fb->slots[i];
                break;
            }
        } else if (free_idx < 0) {
            free_idx = i;
        }
    }

    if (!slot) {
        if (free_idx < 0) {
            /* Evict oldest slot */
            uint64_t oldest = UINT64_MAX;
            for (int i = 0; i < NX_FRAG_REASSEMBLY_SLOTS; i++) {
                if (fb->slots[i].started_ms < oldest) {
                    oldest = fb->slots[i].started_ms;
                    free_idx = i;
                }
            }
        }
        slot = &fb->slots[free_idx];
        memset(slot, 0, sizeof(*slot));
        slot->src        = pkt->header.src;
        slot->frag_id    = fh.frag_id;
        slot->frag_total = fh.frag_total;
        slot->started_ms = now_ms;
        slot->valid      = true;
    }

    /* Verify consistency */
    if (slot->frag_total != fh.frag_total) return NX_ERR_INVALID_ARG;

    /* Skip duplicate fragment */
    uint16_t bit = (uint16_t)(1 << fh.frag_index);
    if (slot->received_mask & bit) {
        return NX_OK; /* Already have this fragment */
    }

    /* Store fragment data at the correct offset */
    size_t frag_offset = (size_t)fh.frag_index * NX_FRAG_PAYLOAD_CAP;
    if (frag_offset + data_in_frag > NX_FRAG_MAX_MESSAGE)
        return NX_ERR_BUFFER_TOO_SMALL;

    memcpy(slot->data + frag_offset, pkt->payload + NX_FRAG_EXTHDR_SIZE, data_in_frag);
    slot->frag_sizes[fh.frag_index] = data_in_frag;
    slot->received_mask |= bit;

    /* Check if all fragments received */
    uint16_t complete_mask = (uint16_t)((1 << fh.frag_total) - 1);
    if (slot->received_mask != complete_mask) {
        return NX_OK; /* Still waiting for more */
    }

    /* All received -- compute total size and copy out */
    size_t total = 0;
    for (int i = 0; i < fh.frag_total; i++) {
        total += slot->frag_sizes[i];
    }

    if (total > out_buf_len) {
        slot->valid = false;
        return NX_ERR_BUFFER_TOO_SMALL;
    }

    /* Copy fragment data contiguously -- each fragment stored at index*CAP */
    size_t out_offset = 0;
    for (int i = 0; i < fh.frag_total; i++) {
        size_t src_offset = (size_t)i * NX_FRAG_PAYLOAD_CAP;
        memcpy(out_data + out_offset, slot->data + src_offset, slot->frag_sizes[i]);
        out_offset += slot->frag_sizes[i];
    }
    *out_len = total;

    slot->valid = false; /* Free the slot */
    return NX_OK;
}
