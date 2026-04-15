/*
 * NEXUS Firmware -- Session Store Persistence
 *
 * One flash entry per valid session slot (mirrors anchor_store.cpp).
 * Each entry is the raw nx_session_t struct (POD).
 */

/* ═══════════════════════════════════════════════════════════════════════
 * ESP32 Implementation (NVS / Preferences)
 * ═══════════════════════════════════════════════════════════════════════ */
#ifdef NX_PLATFORM_ESP32

#include "session_store.h"
#include <Preferences.h>
#include <string.h>
#include <stdio.h>

static Preferences sess_prefs;
static const char *NVS_NS = "nexus_sess";

extern "C" {

nx_err_t nx_session_store_save(const nx_session_store_t *store)
{
    if (!store) return NX_ERR_INVALID_ARG;

    sess_prefs.begin(NVS_NS, false);

    for (int i = 0; i < NX_SESSION_MAX; i++) {
        char key[4];
        snprintf(key, sizeof(key), "s%d", i);

        if (store->sessions[i].valid) {
            sess_prefs.putBytes(key, &store->sessions[i],
                                sizeof(nx_session_t));
        } else {
            sess_prefs.remove(key);
        }
    }

    sess_prefs.end();
    return NX_OK;
}

nx_err_t nx_session_store_load(nx_session_store_t *store)
{
    if (!store) return NX_ERR_INVALID_ARG;

    sess_prefs.begin(NVS_NS, true); /* read-only */

    int loaded = 0;
    for (int i = 0; i < NX_SESSION_MAX; i++) {
        char key[4];
        snprintf(key, sizeof(key), "s%d", i);

        size_t len = sess_prefs.getBytesLength(key);
        if (len == sizeof(nx_session_t)) {
            sess_prefs.getBytes(key, &store->sessions[i],
                                sizeof(nx_session_t));
            if (store->sessions[i].valid) loaded++;
        }
    }

    sess_prefs.end();
    return loaded > 0 ? NX_OK : NX_ERR_NOT_FOUND;
}

nx_err_t nx_session_store_erase(void)
{
    sess_prefs.begin(NVS_NS, false);
    sess_prefs.clear();
    sess_prefs.end();
    return NX_OK;
}

} /* extern "C" */

#endif /* NX_PLATFORM_ESP32 */


/* ═══════════════════════════════════════════════════════════════════════
 * nRF52 Implementation (InternalFS / LittleFS)
 * ═══════════════════════════════════════════════════════════════════════ */
#ifdef NX_PLATFORM_NRF52

#include "session_store.h"
#include <Adafruit_LittleFS.h>
#include <InternalFileSystem.h>
#include <string.h>
#include <stdio.h>

using namespace Adafruit_LittleFS_Namespace;

static void slot_filename(int idx, char *buf, size_t bufsz)
{
    snprintf(buf, bufsz, "/nxs_%d", idx);
}

extern "C" {

nx_err_t nx_session_store_save(const nx_session_store_t *store)
{
    if (!store) return NX_ERR_INVALID_ARG;

    InternalFS.begin();

    for (int i = 0; i < NX_SESSION_MAX; i++) {
        char fname[12];
        slot_filename(i, fname, sizeof(fname));

        if (store->sessions[i].valid) {
            if (InternalFS.exists(fname)) InternalFS.remove(fname);
            File f(InternalFS);
            f.open(fname, FILE_O_WRITE);
            if (f) {
                f.write((const uint8_t *)&store->sessions[i],
                        sizeof(nx_session_t));
                f.close();
            }
        } else {
            if (InternalFS.exists(fname)) InternalFS.remove(fname);
        }
    }

    return NX_OK;
}

nx_err_t nx_session_store_load(nx_session_store_t *store)
{
    if (!store) return NX_ERR_INVALID_ARG;

    InternalFS.begin();

    int loaded = 0;
    for (int i = 0; i < NX_SESSION_MAX; i++) {
        char fname[12];
        slot_filename(i, fname, sizeof(fname));

        if (!InternalFS.exists(fname)) continue;

        File f(InternalFS);
        f.open(fname, FILE_O_READ);
        if (!f) continue;

        size_t read = f.read(&store->sessions[i], sizeof(nx_session_t));
        f.close();

        if (read == sizeof(nx_session_t) && store->sessions[i].valid) {
            loaded++;
        } else {
            store->sessions[i].valid = false;
        }
    }

    return loaded > 0 ? NX_OK : NX_ERR_NOT_FOUND;
}

nx_err_t nx_session_store_erase(void)
{
    InternalFS.begin();

    for (int i = 0; i < NX_SESSION_MAX; i++) {
        char fname[12];
        slot_filename(i, fname, sizeof(fname));
        if (InternalFS.exists(fname)) InternalFS.remove(fname);
    }

    return NX_OK;
}

} /* extern "C" */

#endif /* NX_PLATFORM_NRF52 */
