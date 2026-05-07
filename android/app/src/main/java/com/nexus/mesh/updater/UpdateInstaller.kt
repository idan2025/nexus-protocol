package com.nexus.mesh.updater

import android.app.Activity
import android.content.Context
import android.content.Intent
import android.net.Uri
import android.provider.Settings
import android.util.Log
import androidx.core.content.FileProvider
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.withContext
import okhttp3.OkHttpClient
import okhttp3.Request
import okio.buffer
import okio.sink
import java.io.File
import java.util.concurrent.TimeUnit

/**
 * Downloads a release APK to app cache and hands it to the system
 * package installer. The APK must be signed with the same key as the
 * currently-installed app or Android will refuse with
 * INSTALL_FAILED_UPDATE_INCOMPATIBLE.
 */
class UpdateInstaller(private val context: Context) {

    sealed class State {
        data object Idle : State()
        data class Downloading(val pct: Int, val bytes: Long, val totalBytes: Long) : State()
        data class Ready(val apk: File) : State()
        data class Error(val msg: String) : State()
    }

    private val _state = MutableStateFlow<State>(State.Idle)
    val state: StateFlow<State> = _state

    private val http = OkHttpClient.Builder()
        .connectTimeout(20, TimeUnit.SECONDS)
        .readTimeout(60, TimeUnit.SECONDS)
        .build()

    /** Download the APK. The flow emits Downloading -> Ready / Error. */
    suspend fun download(release: UpdateChecker.ReleaseInfo): File? = withContext(Dispatchers.IO) {
        val dir = File(context.cacheDir, "apk").apply { mkdirs() }
        val outFile = File(dir, "nexus-${release.tag}.apk")
        if (outFile.exists() && outFile.length() == release.sizeBytes) {
            _state.value = State.Ready(outFile)
            return@withContext outFile
        }
        outFile.delete()

        _state.value = State.Downloading(0, 0L, release.sizeBytes)
        val req = Request.Builder().url(release.apkUrl).build()
        try {
            http.newCall(req).execute().use { resp ->
                if (!resp.isSuccessful) {
                    _state.value = State.Error("HTTP ${resp.code}")
                    return@withContext null
                }
                val body = resp.body ?: run {
                    _state.value = State.Error("Empty body")
                    return@withContext null
                }
                val total = if (release.sizeBytes > 0) release.sizeBytes else (body.contentLength().takeIf { it > 0 } ?: 0L)
                val source = body.source()
                outFile.sink().buffer().use { dst ->
                    var written = 0L
                    val buf = okio.Buffer()
                    while (true) {
                        val n = source.read(buf, 64 * 1024)
                        if (n == -1L) break
                        dst.write(buf, n)
                        written += n
                        val pct = if (total > 0) ((written * 100) / total).toInt().coerceIn(0, 100) else 0
                        _state.value = State.Downloading(pct, written, total)
                    }
                    dst.flush()
                }
                _state.value = State.Ready(outFile)
                outFile
            }
        } catch (e: Exception) {
            Log.w(TAG, "download failed", e)
            _state.value = State.Error(e.message ?: "Download failed")
            null
        }
    }

    /**
     * Hand the APK to the system installer. Caller must be an Activity
     * because the install Intent needs an Activity context to render.
     *
     * If the user hasn't granted INSTALL_PACKAGES permission yet, this
     * routes them to the OS settings page first.
     */
    fun install(activity: Activity, apk: File) {
        val pm = activity.packageManager
        if (!pm.canRequestPackageInstalls()) {
            // Send user to the per-app settings to allow installs.
            val intent = Intent(
                Settings.ACTION_MANAGE_UNKNOWN_APP_SOURCES,
                Uri.parse("package:${activity.packageName}")
            ).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
            activity.startActivity(intent)
            return
        }
        val uri = FileProvider.getUriForFile(
            activity, "${activity.packageName}.fileprovider", apk
        )
        val intent = Intent(Intent.ACTION_VIEW).apply {
            setDataAndType(uri, "application/vnd.android.package-archive")
            addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
            addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
        }
        activity.startActivity(intent)
    }

    companion object { private const val TAG = "UpdateInstaller" }
}
