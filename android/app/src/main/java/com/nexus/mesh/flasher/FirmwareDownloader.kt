package com.nexus.mesh.flasher

import android.content.Context
import android.util.Log
import com.nexus.mesh.BuildConfig
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.withContext
import okhttp3.OkHttpClient
import okhttp3.Request
import okio.buffer
import okio.sink
import org.json.JSONObject
import java.io.File
import java.util.concurrent.TimeUnit

/**
 * Downloads firmware binaries from the latest GitHub release into
 * app-private files/firmware/.
 *
 * Files are keyed by tag so re-downloading is idempotent (cache hit
 * returns immediately).
 */
class FirmwareDownloader(private val context: Context) {

    sealed class Progress {
        data object Idle : Progress()
        data class Downloading(val asset: String, val pct: Int) : Progress()
        data class Done(val asset: String, val file: File) : Progress()
        data class Error(val asset: String, val msg: String) : Progress()
    }

    private val _progress = MutableStateFlow<Progress>(Progress.Idle)
    val progress: StateFlow<Progress> = _progress

    private val http = OkHttpClient.Builder()
        .connectTimeout(15, TimeUnit.SECONDS)
        .readTimeout(60, TimeUnit.SECONDS)
        .build()

    private fun firmwareDir(): File =
        File(context.filesDir, "firmware").apply { mkdirs() }

    /**
     * Resolve the latest tag from GitHub. Returns null on network error.
     */
    suspend fun latestTag(): String? = withContext(Dispatchers.IO) {
        val url = "https://api.github.com/repos/${BuildConfig.GITHUB_REPO}/releases/latest"
        val req = Request.Builder()
            .url(url)
            .header("Accept", "application/vnd.github+json")
            .build()
        try {
            http.newCall(req).execute().use { resp ->
                if (!resp.isSuccessful) return@withContext null
                val json = JSONObject(resp.body?.string().orEmpty())
                json.optString("tag_name").ifEmpty { null }
            }
        } catch (e: Exception) {
            Log.w(TAG, "latestTag failed: $e")
            null
        }
    }

    /**
     * Downloads [assetName] from the release tagged [tag]. Cached by
     * (tag, assetName) under filesDir/firmware/.
     */
    suspend fun fetch(tag: String, assetName: String): File? = withContext(Dispatchers.IO) {
        val dir = File(firmwareDir(), tag).apply { mkdirs() }
        val out = File(dir, assetName)
        if (out.exists() && out.length() > 0) {
            _progress.value = Progress.Done(assetName, out)
            return@withContext out
        }

        val url = "https://github.com/${BuildConfig.GITHUB_REPO}/releases/download/$tag/$assetName"
        val req = Request.Builder().url(url).build()
        _progress.value = Progress.Downloading(assetName, 0)
        try {
            http.newCall(req).execute().use { resp ->
                if (!resp.isSuccessful) {
                    _progress.value = Progress.Error(assetName, "HTTP ${resp.code}")
                    return@withContext null
                }
                val body = resp.body ?: run {
                    _progress.value = Progress.Error(assetName, "Empty body")
                    return@withContext null
                }
                val total = body.contentLength().takeIf { it > 0 } ?: 0L
                val source = body.source()
                out.sink().buffer().use { dst ->
                    var written = 0L
                    val buf = okio.Buffer()
                    while (true) {
                        val n = source.read(buf, 64 * 1024)
                        if (n == -1L) break
                        dst.write(buf, n)
                        written += n
                        val pct = if (total > 0) ((written * 100) / total).toInt() else 0
                        _progress.value = Progress.Downloading(assetName, pct)
                    }
                    dst.flush()
                }
                _progress.value = Progress.Done(assetName, out)
                out
            }
        } catch (e: Exception) {
            Log.w(TAG, "fetch $assetName failed: $e")
            _progress.value = Progress.Error(assetName, e.message ?: "Download failed")
            null
        }
    }

    fun reset() { _progress.value = Progress.Idle }

    companion object { private const val TAG = "FirmwareDownloader" }
}
