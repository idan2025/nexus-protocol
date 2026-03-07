/*
 * NEXUS Firmware -- Anchor Mailbox Persistence
 *
 * Stores each valid anchor slot as an individual flash entry.
 * ESP32: NVS via Preferences (keys "a0".."a31")
 * nRF52: InternalFS via LittleFS (files "/nxa_0".."/nxa_31")
 */

/* ═══════════════════════════════════════════════════════════════════════
 * ESP32 Implementation (NVS / Preferences)
 * ═══════════════════════════════════════════════════════════════════════ */
#ifdef NX_PLATFORM_ESP32

#include "anchor_store.h"
#include <Preferences.h>
#include <string.h>
#include <stdio.h>

static Preferences anc_prefs;
static const char *NVS_NS = "nexus_anc";

extern "C" {

nx_err_t nx_anchor_store_save(const nx_anchor_t *anchor)
{
    if (!anchor) return NX_ERR_INVALID_ARG;

    anc_prefs.begin(NVS_NS, false); /* read-write */

    int limit = anchor->max_slots < NX_ANCHOR_MAX_STORED ?
                anchor->max_slots : NX_ANCHOR_MAX_STORED;

    for (int i = 0; i < limit; i++) {
        char key[4];
        snprintf(key, sizeof(key), "a%d", i);

        if (anchor->msgs[i].valid) {
            anc_prefs.putBytes(key, &anchor->msgs[i],
                               sizeof(nx_anchor_msg_t));
        } else {
            anc_prefs.remove(key);
        }
    }

    /* Store slot count so we know how many to load */
    anc_prefs.putInt("cnt", limit);
    anc_prefs.end();

    return NX_OK;
}

nx_err_t nx_anchor_store_load(nx_anchor_t *anchor)
{
    if (!anchor) return NX_ERR_INVALID_ARG;

    anc_prefs.begin(NVS_NS, true); /* read-only */

    int stored_limit = anc_prefs.getInt("cnt", 0);
    int limit = anchor->max_slots < NX_ANCHOR_MAX_STORED ?
                anchor->max_slots : NX_ANCHOR_MAX_STORED;
    if (stored_limit < limit) limit = stored_limit;

    int loaded = 0;
    for (int i = 0; i < limit; i++) {
        char key[4];
        snprintf(key, sizeof(key), "a%d", i);

        size_t len = anc_prefs.getBytesLength(key);
        if (len == sizeof(nx_anchor_msg_t)) {
            anc_prefs.getBytes(key, &anchor->msgs[i],
                               sizeof(nx_anchor_msg_t));
            if (anchor->msgs[i].valid) loaded++;
        }
    }

    anc_prefs.end();

    return loaded > 0 ? NX_OK : NX_ERR_NOT_FOUND;
}

nx_err_t nx_anchor_store_erase(void)
{
    anc_prefs.begin(NVS_NS, false);
    anc_prefs.clear();
    anc_prefs.end();
    return NX_OK;
}

} /* extern "C" */

#endif /* NX_PLATFORM_ESP32 */


/* ═══════════════════════════════════════════════════════════════════════
 * nRF52 Implementation (InternalFS / LittleFS)
 * ═══════════════════════════════════════════════════════════════════════ */
#ifdef NX_PLATFORM_NRF52

#include "anchor_store.h"
#include <Adafruit_LittleFS.h>
#include <InternalFileSystem.h>
#include <string.h>
#include <stdio.h>

using namespace Adafruit_LittleFS_Namespace;

static void slot_filename(int idx, char *buf, size_t bufsz)
{
    snprintf(buf, bufsz, "/nxa_%d", idx);
}

extern "C" {

nx_err_t nx_anchor_store_save(const nx_anchor_t *anchor)
{
    if (!anchor) return NX_ERR_INVALID_ARG;

    InternalFS.begin();

    int limit = anchor->max_slots < NX_ANCHOR_MAX_STORED ?
                anchor->max_slots : NX_ANCHOR_MAX_STORED;

    for (int i = 0; i < limit; i++) {
        char fname[12];
        slot_filename(i, fname, sizeof(fname));

        if (anchor->msgs[i].valid) {
            /* Remove old file first (LittleFS requires this for overwrite) */
            if (InternalFS.exists(fname)) InternalFS.remove(fname);

            File f(InternalFS);
            f.open(fname, FILE_O_WRITE);
            if (f) {
                f.write((const uint8_t *)&anchor->msgs[i],
                        sizeof(nx_anchor_msg_t));
                f.close();
            }
        } else {
            if (InternalFS.exists(fname)) InternalFS.remove(fname);
        }
    }

    return NX_OK;
}

nx_err_t nx_anchor_store_load(nx_anchor_t *anchor)
{
    if (!anchor) return NX_ERR_INVALID_ARG;

    InternalFS.begin();

    int limit = anchor->max_slots < NX_ANCHOR_MAX_STORED ?
                anchor->max_slots : NX_ANCHOR_MAX_STORED;

    int loaded = 0;
    for (int i = 0; i < limit; i++) {
        char fname[12];
        slot_filename(i, fname, sizeof(fname));

        if (!InternalFS.exists(fname)) continue;

        File f(InternalFS);
        f.open(fname, FILE_O_READ);
        if (!f) continue;

        size_t read = f.read(&anchor->msgs[i], sizeof(nx_anchor_msg_t));
        f.close();

        if (read == sizeof(nx_anchor_msg_t) && anchor->msgs[i].valid) {
            loaded++;
        } else {
            anchor->msgs[i].valid = false;
        }
    }

    return loaded > 0 ? NX_OK : NX_ERR_NOT_FOUND;
}

nx_err_t nx_anchor_store_erase(void)
{
    InternalFS.begin();

    for (int i = 0; i < NX_ANCHOR_MAX_STORED; i++) {
        char fname[12];
        slot_filename(i, fname, sizeof(fname));
        if (InternalFS.exists(fname)) InternalFS.remove(fname);
    }

    return NX_OK;
}

} /* extern "C" */

#endif /* NX_PLATFORM_NRF52 */
