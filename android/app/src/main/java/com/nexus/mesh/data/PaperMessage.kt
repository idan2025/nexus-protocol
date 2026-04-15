package com.nexus.mesh.data

import android.util.Base64

/**
 * Compact URL encoding for offline ("paper") message export/import via QR.
 *
 * Format: nexusm://1?f=<8hex from-addr>&t=<unix-seconds hex>&m=<url-safe base64 UTF-8 text>
 *
 * QR capacity in alphanumeric/byte mode is ~2kB, which is larger than any text
 * the NXM envelope supports, so no chunking is needed.
 */
object PaperMessage {
    const val URI_SCHEME = "nexusm"
    const val URI_PREFIX = "nexusm://1?"

    data class Envelope(val fromAddr: String, val timestampMs: Long, val text: String)

    fun encode(env: Envelope): String {
        val textB64 = Base64.encodeToString(
            env.text.toByteArray(Charsets.UTF_8),
            Base64.URL_SAFE or Base64.NO_WRAP or Base64.NO_PADDING
        )
        val tsHex = (env.timestampMs / 1000L).toString(16)
        return "$URI_PREFIX" +
            "f=${env.fromAddr.uppercase()}" +
            "&t=$tsHex" +
            "&m=$textB64"
    }

    fun decode(uri: String): Envelope? {
        if (!uri.startsWith(URI_PREFIX)) return null
        val params = uri.removePrefix(URI_PREFIX).split("&")
            .mapNotNull {
                val eq = it.indexOf('=')
                if (eq <= 0) null else it.substring(0, eq) to it.substring(eq + 1)
            }.toMap()
        val from = params["f"]?.trim()?.uppercase() ?: return null
        if (from.length != 8 || !from.all { c -> c.isDigit() || c in 'A'..'F' }) return null
        val ts = params["t"]?.toLongOrNull(16) ?: return null
        val textB64 = params["m"] ?: return null
        val text = try {
            String(
                Base64.decode(textB64, Base64.URL_SAFE or Base64.NO_WRAP or Base64.NO_PADDING),
                Charsets.UTF_8
            )
        } catch (_: IllegalArgumentException) {
            return null
        }
        return Envelope(from, ts * 1000L, text)
    }
}
