package com.nexus.mesh.updater

import android.app.Activity
import android.content.Context
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
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
import java.util.Calendar
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

/**
 * Modal dialog shown automatically when a newer release is available.
 * Surfaces the GitHub release notes (changelog) and lets the user pick
 * between Update now / Later / Schedule.
 *
 * Place this inside the root composable; it short-circuits to no-op
 * when there's no pending update or when the user has already chosen
 * Skip / Later / Schedule.
 */
@Composable
fun UpdateAvailableDialog(state: UpdateState, activity: Activity, forceShow: Boolean = false) {
    val rel = state.latest ?: return
    if (!state.checker.isNewer(rel)) return
    if (state.checker.isSkipped(rel.tag)) return
    if (!forceShow && state.checker.isSnoozed(rel.tag)) return
    if (!forceShow && UpdateScheduler.hasPending(activity, rel.tag)) return

    var dismissed by remember(rel.tag) { mutableStateOf(false) }
    if (dismissed) return

    var showSchedule by remember { mutableStateOf(false) }
    val scope = rememberCoroutineScope()
    val installState by state.installer.state.collectAsState()

    AlertDialog(
        onDismissRequest = {
            // Treat outside-tap / back as "Later" so the user is never
            // trapped, but we still respect their choice across the session.
            state.checker.snoozeUntil(
                rel.tag,
                System.currentTimeMillis() + UpdateChecker.LATER_SNOOZE_MS
            )
            dismissed = true
        },
        icon = { Icon(Icons.Default.SystemUpdate, contentDescription = null) },
        title = { Text("Update available: ${rel.tag}") },
        text = {
            Column {
                Text(
                    "You're on ${BuildConfig.VERSION_NAME}",
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
                Spacer(Modifier.height(10.dp))
                Text("What's new", fontWeight = FontWeight.SemiBold)
                Spacer(Modifier.height(4.dp))
                val notes = rel.notes.trim().ifBlank { "(No release notes provided.)" }
                Box(
                    modifier = Modifier
                        .fillMaxWidth()
                        .heightIn(max = 240.dp)
                        .verticalScroll(rememberScrollState())
                ) {
                    Text(
                        notes,
                        style = MaterialTheme.typography.bodySmall
                    )
                }
                when (val s = installState) {
                    is UpdateInstaller.State.Downloading -> {
                        Spacer(Modifier.height(8.dp))
                        LinearProgressIndicator(
                            progress = { s.pct / 100f },
                            modifier = Modifier.fillMaxWidth()
                        )
                        Text(
                            "${s.pct}%",
                            style = MaterialTheme.typography.bodySmall
                        )
                    }
                    is UpdateInstaller.State.Error -> {
                        Spacer(Modifier.height(8.dp))
                        Text(s.msg, color = MaterialTheme.colorScheme.error,
                             style = MaterialTheme.typography.bodySmall)
                    }
                    else -> Unit
                }
            }
        },
        confirmButton = {
            when (val s = installState) {
                is UpdateInstaller.State.Ready ->
                    TextButton(onClick = {
                        state.installer.install(activity, s.apk)
                        dismissed = true
                    }) { Text("Install") }
                is UpdateInstaller.State.Downloading ->
                    TextButton(onClick = { }, enabled = false) { Text("${s.pct}%") }
                else ->
                    TextButton(onClick = {
                        state.checker.clearSnooze()
                        UpdateScheduler.cancel(activity)
                        scope.launch { state.installer.download(rel) }
                    }) { Text("Update now") }
            }
        },
        dismissButton = {
            Row {
                TextButton(onClick = { showSchedule = true }) { Text("Schedule") }
                Spacer(Modifier.width(4.dp))
                TextButton(onClick = {
                    state.checker.snoozeUntil(
                        rel.tag,
                        System.currentTimeMillis() + UpdateChecker.LATER_SNOOZE_MS
                    )
                    dismissed = true
                }) { Text("Later") }
            }
        }
    )

    if (showSchedule) {
        ScheduleUpdateDialog(
            onDismiss = { showSchedule = false },
            onPick = { atMs ->
                UpdateScheduler.schedule(activity, atMs, rel.tag)
                showSchedule = false
                dismissed = true
            }
        )
    }
}

@Composable
private fun ScheduleUpdateDialog(
    onDismiss: () -> Unit,
    onPick: (Long) -> Unit,
) {
    val now = System.currentTimeMillis()
    val options = listOf(
        "In 1 hour"     to now + 60L * 60_000L,
        "In 4 hours"    to now + 4L * 60L * 60_000L,
        "Tonight 11 PM" to nextHour(23),
        "Tomorrow 9 AM" to nextHour(9),
    )
    AlertDialog(
        onDismissRequest = onDismiss,
        icon = { Icon(Icons.Default.SystemUpdate, contentDescription = null) },
        title = { Text("Schedule update") },
        text = {
            Column {
                Text(
                    "We'll remind you to install at:",
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
                Spacer(Modifier.height(8.dp))
                options.forEach { (label, ms) ->
                    Surface(
                        modifier = Modifier
                            .fillMaxWidth()
                            .padding(vertical = 2.dp),
                        shape = RoundedCornerShape(8.dp),
                        color = MaterialTheme.colorScheme.surfaceVariant
                    ) {
                        TextButton(
                            onClick = { onPick(ms) },
                            modifier = Modifier.fillMaxWidth()
                        ) {
                            Row(
                                modifier = Modifier.fillMaxWidth(),
                                verticalAlignment = Alignment.CenterVertically
                            ) {
                                Text(label, modifier = Modifier.weight(1f))
                                Text(
                                    formatScheduleTime(ms),
                                    style = MaterialTheme.typography.bodySmall,
                                    color = MaterialTheme.colorScheme.onSurfaceVariant
                                )
                            }
                        }
                    }
                }
            }
        },
        confirmButton = { },
        dismissButton = {
            TextButton(onClick = onDismiss) { Text("Cancel") }
        }
    )
}

private fun nextHour(hour: Int): Long {
    val cal = Calendar.getInstance()
    cal.set(Calendar.HOUR_OF_DAY, hour)
    cal.set(Calendar.MINUTE, 0)
    cal.set(Calendar.SECOND, 0)
    cal.set(Calendar.MILLISECOND, 0)
    if (cal.timeInMillis <= System.currentTimeMillis()) {
        cal.add(Calendar.DAY_OF_YEAR, 1)
    }
    return cal.timeInMillis
}

private fun formatScheduleTime(ms: Long): String {
    val now = System.currentTimeMillis()
    val sameDay = run {
        val a = Calendar.getInstance().apply { timeInMillis = now }
        val b = Calendar.getInstance().apply { timeInMillis = ms }
        a.get(Calendar.DAY_OF_YEAR) == b.get(Calendar.DAY_OF_YEAR) &&
            a.get(Calendar.YEAR) == b.get(Calendar.YEAR)
    }
    return if (sameDay) {
        DateFormat.getTimeInstance(DateFormat.SHORT).format(Date(ms))
    } else {
        DateFormat.getDateTimeInstance(DateFormat.SHORT, DateFormat.SHORT).format(Date(ms))
    }
}
