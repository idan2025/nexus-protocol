/*
 * NEXUS Firmware -- Identity Persistence (ESP32 NVS)
 */
#ifdef NX_PLATFORM_ESP32

#include "identity_store.h"
#include <Preferences.h>
#include <string.h>

static Preferences prefs;
static const char *NVS_NS = "nexus";
static const char *NVS_KEY = "identity";

extern "C" {

nx_err_t nx_identity_store_load(nx_identity_t *id)
{
    if (!id) return NX_ERR_INVALID_ARG;

    prefs.begin(NVS_NS, true);  /* read-only */
    size_t len = prefs.getBytesLength(NVS_KEY);

    if (len != sizeof(nx_identity_t)) {
        prefs.end();
        return NX_ERR_NOT_FOUND;
    }

    prefs.getBytes(NVS_KEY, id, sizeof(nx_identity_t));
    prefs.end();

    /* Verify loaded identity is valid (sign_public not all zeros) */
    uint8_t zeros[32] = {0};
    if (memcmp(id->sign_public, zeros, 32) == 0) {
        return NX_ERR_NOT_FOUND;
    }

    return NX_OK;
}

nx_err_t nx_identity_store_save(const nx_identity_t *id)
{
    if (!id) return NX_ERR_INVALID_ARG;

    prefs.begin(NVS_NS, false);  /* read-write */
    size_t written = prefs.putBytes(NVS_KEY, id, sizeof(nx_identity_t));
    prefs.end();

    return (written == sizeof(nx_identity_t)) ? NX_OK : NX_ERR_IO;
}

nx_err_t nx_identity_store_erase(void)
{
    prefs.begin(NVS_NS, false);
    prefs.remove(NVS_KEY);
    prefs.end();
    return NX_OK;
}

} /* extern "C" */

#endif /* NX_PLATFORM_ESP32 */
