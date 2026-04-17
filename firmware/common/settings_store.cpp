/*
 * NEXUS Firmware -- Settings Persistence
 *
 * ESP32: NVS via Preferences (single blob "settings")
 * nRF52: InternalFS via LittleFS (file "/nexus_cfg")
 */

/* ===================================================================
 * ESP32 Implementation (NVS / Preferences)
 * =================================================================== */
#ifdef NX_PLATFORM_ESP32

#include "settings_store.h"
#include <Preferences.h>
#include <string.h>

static Preferences cfg_prefs;
static const char *NVS_NS  = "nexus_cfg";
static const char *NVS_KEY = "settings";

extern "C" {

nx_err_t nx_settings_load(nx_settings_t *settings)
{
    if (!settings) return NX_ERR_INVALID_ARG;

    cfg_prefs.begin(NVS_NS, true); /* read-only */
    size_t len = cfg_prefs.getBytesLength(NVS_KEY);

    if (len != sizeof(nx_settings_t)) {
        cfg_prefs.end();
        return NX_ERR_NOT_FOUND;
    }

    cfg_prefs.getBytes(NVS_KEY, settings, sizeof(nx_settings_t));
    cfg_prefs.end();

    if (settings->magic != NX_SETTINGS_MAGIC) {
        return NX_ERR_NOT_FOUND;
    }

    return NX_OK;
}

nx_err_t nx_settings_save(const nx_settings_t *settings)
{
    if (!settings) return NX_ERR_INVALID_ARG;

    cfg_prefs.begin(NVS_NS, false); /* read-write */
    size_t written = cfg_prefs.putBytes(NVS_KEY, settings,
                                         sizeof(nx_settings_t));
    cfg_prefs.end();

    return (written == sizeof(nx_settings_t)) ? NX_OK : NX_ERR_IO;
}

nx_err_t nx_settings_erase(void)
{
    cfg_prefs.begin(NVS_NS, false);
    cfg_prefs.clear();
    cfg_prefs.end();
    return NX_OK;
}

} /* extern "C" */

#endif /* NX_PLATFORM_ESP32 */


/* ===================================================================
 * nRF52 Implementation (InternalFS / LittleFS)
 * =================================================================== */
#ifdef NX_PLATFORM_NRF52

#include "settings_store.h"
#include <Adafruit_LittleFS.h>
#include <InternalFileSystem.h>
#include <string.h>

using namespace Adafruit_LittleFS_Namespace;

static const char *CFG_FILE = "/nexus_cfg";

extern "C" {

nx_err_t nx_settings_load(nx_settings_t *settings)
{
    if (!settings) return NX_ERR_INVALID_ARG;

    File f(InternalFS);
    f.open(CFG_FILE, FILE_O_READ);
    if (!f) return NX_ERR_NOT_FOUND;

    size_t rd = f.read(settings, sizeof(nx_settings_t));
    f.close();

    if (rd != sizeof(nx_settings_t) || settings->magic != NX_SETTINGS_MAGIC) {
        return NX_ERR_NOT_FOUND;
    }

    return NX_OK;
}

nx_err_t nx_settings_save(const nx_settings_t *settings)
{
    if (!settings) return NX_ERR_INVALID_ARG;

    if (InternalFS.exists(CFG_FILE)) InternalFS.remove(CFG_FILE);

    File f(InternalFS);
    f.open(CFG_FILE, FILE_O_WRITE);
    if (!f) return NX_ERR_IO;

    size_t written = f.write((const uint8_t *)settings,
                              sizeof(nx_settings_t));
    f.close();

    return (written == sizeof(nx_settings_t)) ? NX_OK : NX_ERR_IO;
}

nx_err_t nx_settings_erase(void)
{
    if (InternalFS.exists(CFG_FILE)) InternalFS.remove(CFG_FILE);
    return NX_OK;
}

} /* extern "C" */

#endif /* NX_PLATFORM_NRF52 */
