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
     *
     * Side effect: drops any previously-cached firmware under other
     * tag dirs so we never carry more than one release worth of
     * binaries on disk.
     */
    suspend fun fetch(tag: String, assetName: String): File? = withContext(Dispatchers.IO) {
        cleanStaleTagsExcept(tag)
        val dir = File(firmwareDir(), tag).apply { mkdirs() }
        val out = File(dir, assetName)
        // Cache hit only if the local file is non-empty AND its size
        // matches what the release currently advertises. Re-using a
        // stale cache after a re-upload under the same tag was the
        // most common "binary already downloaded" trap -- CI sometimes
        // overwrites release assets without bumping the tag.
        if (out.exists() && out.length() > 0) {
            val remoteSize = headSize(tag, assetName)
            if (remoteSize == null || remoteSize == out.length()) {
                _progress.value = Progress.Done(assetName, out)
                return@withContext out
            }
            Log.i(TAG, "Cache size mismatch for $assetName " +
                       "(local=${out.length()}, remote=$remoteSize) — re-downloading")
            out.delete()
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

    /**
     * Delete cached firmware for any tag except [keepTag]. Keeps disk
     * usage to a single release's worth of binaries.
     */
    fun cleanStaleTagsExcept(keepTag: String?) {
        val root = File(context.filesDir, "firmware")
        if (!root.exists()) return
        var bytes = 0L
        root.listFiles()?.forEach { d ->
            if (d.isDirectory && d.name != keepTag) {
                bytes += dirSize(d)
                d.deleteRecursively()
            }
        }
        if (bytes > 0) Log.i(TAG, "Pruned ${bytes / 1024} KB of stale firmware")
    }

    /** Wipe every cached firmware image (e.g. on app start). */
    fun cleanAll() = cleanStaleTagsExcept(null)

    /**
     * HEAD the release asset and return Content-Length, or null on any
     * failure (network down, redirect chain stripped header, etc.).
     * Used to invalidate cache hits when CI re-uploaded an asset under
     * the same tag.
     */
    private fun headSize(tag: String, assetName: String): Long? {
        val url = "https://github.com/${BuildConfig.GITHUB_REPO}/releases/download/$tag/$assetName"
        val req = Request.Builder().url(url).head().build()
        return try {
            http.newCall(req).execute().use { resp ->
                if (!resp.isSuccessful) return null
                resp.header("Content-Length")?.toLongOrNull()
            }
        } catch (e: Exception) {
            Log.w(TAG, "headSize $assetName failed: $e"); null
        }
    }

    private fun dirSize(d: File): Long {
        var sum = 0L
        d.listFiles()?.forEach { sum += if (it.isDirectory) dirSize(it) else it.length() }
        return sum
    }

    companion object { private const val TAG = "FirmwareDownloader" }
}
