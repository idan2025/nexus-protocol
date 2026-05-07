package com.nexus.mesh.updater

import android.content.Context
import android.util.Log
import com.nexus.mesh.BuildConfig
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import okhttp3.OkHttpClient
import okhttp3.Request
import org.json.JSONObject
import java.util.concurrent.TimeUnit

/**
 * Polls GitHub Releases for a newer signed APK.
 *
 * The release workflow uploads `app-release.apk` (signed with the
 * project's stable keystore) on every tag. We compare its tag_name
 * against [BuildConfig.VERSION_NAME] and surface the result through
 * [latestRelease].
 */
class UpdateChecker(private val context: Context) {

    private val http = OkHttpClient.Builder()
        .connectTimeout(15, TimeUnit.SECONDS)
        .readTimeout(15, TimeUnit.SECONDS)
        .build()

    data class ReleaseInfo(
        val tag: String,
        val version: SemVer,
        val apkUrl: String,
        val sizeBytes: Long,
        val htmlUrl: String,
        val notes: String,
    )

    /**
     * Fetch the latest release. Returns null on network error or if the
     * release has no APK asset (e.g. while the workflow is still
     * building).
     */
    suspend fun fetchLatest(): ReleaseInfo? = withContext(Dispatchers.IO) {
        val url = "https://api.github.com/repos/${BuildConfig.GITHUB_REPO}/releases/latest"
        val req = Request.Builder()
            .url(url)
            .header("Accept", "application/vnd.github+json")
            .header("User-Agent", "NEXUS-Mesh-Updater")
            .build()
        try {
            http.newCall(req).execute().use { resp ->
                if (!resp.isSuccessful) {
                    Log.w(TAG, "GitHub API ${resp.code}")
                    return@withContext null
                }
                val json = JSONObject(resp.body?.string().orEmpty())
                val tag = json.optString("tag_name").ifEmpty { return@withContext null }
                val ver = SemVer.parse(tag) ?: return@withContext null
                val htmlUrl = json.optString("html_url")
                val notes = json.optString("body")
                val assets = json.optJSONArray("assets") ?: return@withContext null
                for (i in 0 until assets.length()) {
                    val a = assets.getJSONObject(i)
                    val name = a.optString("name")
                    if (name.endsWith(".apk")) {
                        return@withContext ReleaseInfo(
                            tag       = tag,
                            version   = ver,
                            apkUrl    = a.optString("browser_download_url"),
                            sizeBytes = a.optLong("size"),
                            htmlUrl   = htmlUrl,
                            notes     = notes,
                        )
                    }
                }
                Log.w(TAG, "Release $tag has no APK asset yet")
                null
            }
        } catch (e: Exception) {
            Log.w(TAG, "fetchLatest failed: $e")
            null
        }
    }

    /**
     * Returns true if [release] is strictly newer than the installed
     * BuildConfig.VERSION_NAME.
     */
    fun isNewer(release: ReleaseInfo): Boolean {
        val cur = SemVer.parse(BuildConfig.VERSION_NAME) ?: return false
        return release.version > cur
    }

    /** Persist last-check timestamp so we can rate-limit auto checks. */
    fun markChecked() {
        prefs().edit().putLong(KEY_LAST_CHECK, System.currentTimeMillis()).apply()
    }

    fun lastCheckMs(): Long = prefs().getLong(KEY_LAST_CHECK, 0L)

    /** Skip a given tag — used when the user dismisses the banner. */
    fun skipTag(tag: String) {
        prefs().edit().putString(KEY_SKIPPED_TAG, tag).apply()
    }

    fun isSkipped(tag: String): Boolean = prefs().getString(KEY_SKIPPED_TAG, null) == tag

    private fun prefs() = context.getSharedPreferences("nexus_updater", Context.MODE_PRIVATE)

    companion object {
        private const val TAG = "UpdateChecker"
        private const val KEY_LAST_CHECK = "last_check_ms"
        private const val KEY_SKIPPED_TAG = "skipped_tag"
        const val AUTO_CHECK_INTERVAL_MS = 24L * 60L * 60L * 1000L // 24 h
    }
}
