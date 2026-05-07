package com.nexus.mesh.flasher

/**
 * Hardcoded mapping from supported board -> the artifacts the GitHub
 * release exposes for it.
 *
 * This is intentionally a static catalog so the Flash Node screen can
 * render with sensible defaults even when the network's offline; the
 * actual download URLs are constructed at runtime from the latest
 * release tag.
 */
data class BoardSpec(
    val id: String,            // matches release asset prefix (heltec_v3, etc.)
    val displayName: String,
    val mcu: String,
    val hasUsbFlash: Boolean,  // ESP32 family -> USB-OTG flashable
    val hasBleDfu: Boolean,    // nRF52 family -> Nordic DFU flashable
    val webflashAsset: String? = null,
    val uf2Asset: String? = null,
    val dfuZipAsset: String? = null,
    /** Short summary shown on the card. */
    val tagline: String,
)

object FirmwareCatalog {
    val BOARDS = listOf(
        BoardSpec(
            id            = "heltec_v3",
            displayName   = "Heltec V3",
            mcu           = "ESP32-S3 + SX1262",
            hasUsbFlash   = true,
            hasBleDfu     = false,
            webflashAsset = "nexus-heltec_v3-webflash.bin",
            tagline       = "OLED display, integrated USB-UART (CP210x)."
        ),
        BoardSpec(
            id            = "xiao_esp32s3",
            displayName   = "XIAO ESP32S3",
            mcu           = "ESP32-S3 + WIO-SX1262",
            hasUsbFlash   = true,
            hasBleDfu     = false,
            webflashAsset = "nexus-xiao_esp32s3-webflash.bin",
            tagline       = "Headless. Native USB-Serial-JTAG (experimental)."
        ),
        BoardSpec(
            id            = "rak4631",
            displayName   = "RAK4631",
            mcu           = "nRF52840 + SX1262",
            hasUsbFlash   = false,
            hasBleDfu     = true,
            uf2Asset      = "nexus-rak4631.uf2",
            dfuZipAsset   = "nexus-rak4631-dfu.zip",
            tagline       = "WisBlock. UF2 drag-and-drop, or BLE DFU OTA."
        ),
        BoardSpec(
            id            = "xiao_nrf52840",
            displayName   = "XIAO nRF52840",
            mcu           = "nRF52840 + WIO-SX1262",
            hasUsbFlash   = false,
            hasBleDfu     = true,
            uf2Asset      = "nexus-xiao_nrf52840.uf2",
            dfuZipAsset   = "nexus-xiao_nrf52840-dfu.zip",
            tagline       = "Headless nRF52. UF2 drag-and-drop, or BLE DFU OTA."
        ),
    )

    fun byId(id: String): BoardSpec? = BOARDS.firstOrNull { it.id == id }
}
