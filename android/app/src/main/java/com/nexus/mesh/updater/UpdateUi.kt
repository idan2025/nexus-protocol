package com.nexus.mesh.updater

import android.app.Activity
import android.content.Context
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.SystemUpdate
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import com.nexus.mesh.BuildConfig
import kotlinx.coroutines.launch
import java.text.DateFormat
import java.util.Date

/**
 * Shared, app-scoped update state. Owned by MainActivity, observed by
 * the home banner and by SettingsScreen.
 */
class UpdateState(private val context: Context) {
    val checker = UpdateChecker(context)
    val installer = UpdateInstaller(context)

    var latest by mutableStateOf<UpdateChecker.ReleaseInfo?>(null)
        private set
    var checking by mutableStateOf(false)
        private set
    var lastError by mutableStateOf<String?>(null)
        private set

    suspend fun refresh(force: Boolean = false): UpdateChecker.ReleaseInfo? {
        val now = System.currentTimeMillis()
        if (!force && now - checker.lastCheckMs() < UpdateChecker.AUTO_CHECK_INTERVAL_MS) {
            return latest
        }
        checking = true
        lastError = null
        try {
            val rel = checker.fetchLatest()
            checker.markChecked()
            latest = rel
            return rel
        } catch (e: Exception) {
            lastError = e.message
            return null
        } finally {
            checking = false
        }
    }
}

@Composable
fun rememberUpdateState(context: Context): UpdateState =
    remember { UpdateState(context) }

/**
 * Slim banner shown above the home screen when an update is available.
 * Tapping "Update" downloads + installs.
 */
@Composable
fun UpdateBanner(state: UpdateState, activity: Activity) {
    val rel = state.latest ?: return
    if (!state.checker.isNewer(rel)) return
    if (state.checker.isSkipped(rel.tag)) return

    val scope = rememberCoroutineScope()
    val installState by state.installer.state.collectAsState()

    Surface(
        modifier = Modifier.fillMaxWidth().padding(8.dp),
        shape = RoundedCornerShape(10.dp),
        color = MaterialTheme.colorScheme.primaryContainer,
        tonalElevation = 2.dp
    ) {
        Row(
            modifier = Modifier.padding(12.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            Icon(Icons.Default.SystemUpdate, contentDescription = null,
                 tint = MaterialTheme.colorScheme.primary)
            Spacer(Modifier.width(10.dp))
            Column(modifier = Modifier.weight(1f)) {
                Text(
                    "Update ${rel.tag} available",
                    fontWeight = FontWeight.SemiBold,
                    style = MaterialTheme.typography.bodyMedium
                )
                Text(
                    "You're on ${BuildConfig.VERSION_NAME}",
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
                when (val s = installState) {
                    is UpdateInstaller.State.Downloading ->
                        LinearProgressIndicator(
                            progress = { s.pct / 100f },
                            modifier = Modifier.fillMaxWidth().padding(top = 4.dp)
                        )
                    is UpdateInstaller.State.Error ->
                        Text(s.msg, color = MaterialTheme.colorScheme.error,
                             style = MaterialTheme.typography.bodySmall)
                    else -> Unit
                }
            }
            Spacer(Modifier.width(8.dp))
            when (val s = installState) {
                is UpdateInstaller.State.Ready ->
                    Button(onClick = { state.installer.install(activity, s.apk) }) { Text("Install") }
                is UpdateInstaller.State.Downloading -> Text("${s.pct}%")
                else -> {
                    TextButton(onClick = { state.checker.skipTag(rel.tag) }) { Text("Skip") }
                    Button(onClick = {
                        scope.launch { state.installer.download(rel) }
                    }) { Text("Update") }
                }
            }
        }
    }
}

/** Used by SettingsScreen — full row with last-check timestamp. */
@Composable
fun UpdateSettingsRow(state: UpdateState, activity: Activity) {
    val scope = rememberCoroutineScope()
    val installState by state.installer.state.collectAsState()
    val rel = state.latest

    val statusLine = when {
        state.checking -> "Checking…"
        state.lastError != null -> "Error: ${state.lastError}"
        rel == null -> "Last check: ${formatTs(state.checker.lastCheckMs())}"
        state.checker.isNewer(rel) -> "Update available: ${rel.tag}"
        else -> "Up to date (${BuildConfig.VERSION_NAME})"
    }

    Card(modifier = Modifier.fillMaxWidth().padding(8.dp)) {
        Column(modifier = Modifier.padding(16.dp)) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Icon(Icons.Default.SystemUpdate, contentDescription = null)
                Spacer(Modifier.width(10.dp))
                Column(modifier = Modifier.weight(1f)) {
                    Text("App Updates", fontWeight = FontWeight.SemiBold)
                    Text(statusLine, style = MaterialTheme.typography.bodySmall,
                         color = MaterialTheme.colorScheme.onSurfaceVariant)
                }
                OutlinedButton(onClick = { scope.launch { state.refresh(force = true) } }) {
                    Text(if (state.checking) "..." else "Check now")
                }
            }
            if (rel != null && state.checker.isNewer(rel)) {
                Spacer(Modifier.height(8.dp))
                when (val s = installState) {
                    is UpdateInstaller.State.Downloading -> {
                        LinearProgressIndicator(
                            progress = { s.pct / 100f },
                            modifier = Modifier.fillMaxWidth()
                        )
                        Text("${s.pct}% — ${s.bytes / 1024} / ${s.totalBytes / 1024} KB",
                             style = MaterialTheme.typography.bodySmall)
                    }
                    is UpdateInstaller.State.Ready ->
                        Button(
                            onClick = { state.installer.install(activity, s.apk) },
                            modifier = Modifier.fillMaxWidth()
                        ) { Text("Install ${rel.tag}") }
                    is UpdateInstaller.State.Error ->
                        Text(s.msg, color = MaterialTheme.colorScheme.error)
                    else ->
                        Button(
                            onClick = { scope.launch { state.installer.download(rel) } },
                            modifier = Modifier.fillMaxWidth()
                        ) { Text("Download ${rel.tag} (${rel.sizeBytes / 1024 / 1024} MB)") }
                }
            }
        }
    }
}

private fun formatTs(ms: Long): String {
    if (ms <= 0L) return "never"
    return DateFormat.getDateTimeInstance(DateFormat.SHORT, DateFormat.SHORT)
        .format(Date(ms))
}
