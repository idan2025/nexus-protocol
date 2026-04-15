package com.nexus.mesh.ui

import androidx.compose.foundation.layout.*
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.ArrowBack
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.unit.dp
import androidx.navigation.NavController
import com.nexus.mesh.service.NexusNode
import kotlinx.coroutines.delay

private fun roleName(r: Int): String = when (r) {
    0 -> "LEAF"
    1 -> "RELAY"
    2 -> "GATEWAY"
    3 -> "ANCHOR"
    4 -> "SENTINEL"
    5 -> "PILLAR"
    6 -> "VAULT"
    else -> "UNKNOWN($r)"
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun TelemetryScreen(activity: MainActivity, navController: NavController) {
    val service = activity.getService()
    var telemetry by remember { mutableStateOf<NexusNode.Telemetry?>(null) }
    var ticks by remember { mutableStateOf(0) }

    LaunchedEffect(Unit) {
        while (true) {
            telemetry = service?.getTelemetry()
            ticks++
            delay(2000)
        }
    }

    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text("Node Telemetry") },
                navigationIcon = {
                    IconButton(onClick = { navController.popBackStack() }) {
                        Icon(Icons.Default.ArrowBack, contentDescription = "Back")
                    }
                },
                actions = {
                    TextButton(onClick = { navController.navigate("routes") }) {
                        Text("Routes")
                    }
                }
            )
        }
    ) { padding ->
        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(padding)
                .padding(16.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            val t = telemetry
            if (t == null) {
                Text("Waiting for node...", style = MaterialTheme.typography.bodyMedium)
                return@Column
            }

            StatusCard(t)
            CapacityCard("Neighbors",       t.neighbors.toLong(),    null)
            CapacityCard("Routes",          t.routesActive.toLong(), t.routesMax.toLong())
            CapacityCard("Anchor mailbox",  t.anchorUsed.toLong(),   t.anchorMax.toLong())
            CapacityCard("Sessions",        t.sessions.toLong(),     t.sessionsMax.toLong())
            CapacityCard("Transports up",   t.transports.toLong(),   null)

            Text(
                "Refreshes every 2s · tick #$ticks",
                style = MaterialTheme.typography.labelSmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
                modifier = Modifier.align(Alignment.End)
            )
        }
    }
}

@Composable
private fun StatusCard(t: NexusNode.Telemetry) {
    Card(modifier = Modifier.fillMaxWidth()) {
        Column(modifier = Modifier.padding(16.dp)) {
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically,
            ) {
                Text("Status", style = MaterialTheme.typography.titleMedium)
                AssistChip(
                    onClick = {},
                    label = { Text(if (t.running) "RUNNING" else "STOPPED") }
                )
            }
            Spacer(Modifier.height(8.dp))
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween
            ) {
                Text("Role", style = MaterialTheme.typography.bodyMedium)
                Text(
                    roleName(t.role),
                    style = MaterialTheme.typography.bodyMedium,
                    fontFamily = FontFamily.Monospace
                )
            }
        }
    }
}

@Composable
private fun CapacityCard(label: String, used: Long, capacity: Long?) {
    Card(modifier = Modifier.fillMaxWidth()) {
        Column(modifier = Modifier.padding(16.dp)) {
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically,
            ) {
                Text(label, style = MaterialTheme.typography.bodyMedium)
                val display = if (capacity != null) "$used / $capacity" else "$used"
                Text(
                    display,
                    style = MaterialTheme.typography.titleSmall,
                    fontFamily = FontFamily.Monospace,
                    color = MaterialTheme.colorScheme.primary
                )
            }
            if (capacity != null && capacity > 0) {
                Spacer(Modifier.height(8.dp))
                LinearProgressIndicator(
                    progress = { (used.toFloat() / capacity.toFloat()).coerceIn(0f, 1f) },
                    modifier = Modifier.fillMaxWidth()
                )
            }
        }
    }
}
