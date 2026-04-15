package com.nexus.mesh.ui

import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
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

private fun roleShort(r: Int): String = when (r) {
    0 -> "LEAF"
    1 -> "RELAY"
    2 -> "GW"
    3 -> "ANCH"
    4 -> "SENT"
    5 -> "PILR"
    6 -> "VAULT"
    else -> "?"
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun RouteInspectorScreen(activity: MainActivity, navController: NavController) {
    val service = activity.getService()
    var routes by remember { mutableStateOf<List<NexusNode.RouteRow>>(emptyList()) }
    var neighbors by remember { mutableStateOf<List<NexusNode.NeighborRow>>(emptyList()) }
    var tab by remember { mutableStateOf(0) }

    LaunchedEffect(Unit) {
        while (true) {
            routes = service?.listRoutes().orEmpty()
            neighbors = service?.listNeighborsDetailed().orEmpty()
            delay(2500)
        }
    }

    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text("Route Inspector") },
                navigationIcon = {
                    IconButton(onClick = { navController.popBackStack() }) {
                        Icon(Icons.Default.ArrowBack, contentDescription = "Back")
                    }
                }
            )
        }
    ) { padding ->
        Column(modifier = Modifier.fillMaxSize().padding(padding)) {
            TabRow(selectedTabIndex = tab) {
                Tab(selected = tab == 0, onClick = { tab = 0 },
                    text = { Text("Neighbors (${neighbors.size})") })
                Tab(selected = tab == 1, onClick = { tab = 1 },
                    text = { Text("Routes (${routes.size})") })
            }
            when (tab) {
                0 -> NeighborList(neighbors)
                1 -> RouteList(routes)
            }
        }
    }
}

@Composable
private fun NeighborList(rows: List<NexusNode.NeighborRow>) {
    if (rows.isEmpty()) {
        EmptyMsg("No direct neighbors yet. Run /announce to broadcast.")
        return
    }
    LazyColumn(
        modifier = Modifier.fillMaxSize(),
        contentPadding = PaddingValues(16.dp),
        verticalArrangement = Arrangement.spacedBy(8.dp)
    ) {
        items(rows) { n ->
            Card(modifier = Modifier.fillMaxWidth()) {
                Column(modifier = Modifier.padding(12.dp)) {
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.SpaceBetween,
                        verticalAlignment = Alignment.CenterVertically,
                    ) {
                        Text(
                            n.addr,
                            style = MaterialTheme.typography.titleSmall,
                            fontFamily = FontFamily.Monospace
                        )
                        AssistChip(onClick = {}, label = { Text(roleShort(n.role)) })
                    }
                    Spacer(Modifier.height(4.dp))
                    Text(
                        "rssi=${n.rssi} dBm  lq=${n.linkQuality}/255  age=${n.ageSec}s",
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                        fontFamily = FontFamily.Monospace
                    )
                }
            }
        }
    }
}

@Composable
private fun RouteList(rows: List<NexusNode.RouteRow>) {
    if (rows.isEmpty()) {
        EmptyMsg("No multi-hop routes learned yet.")
        return
    }
    LazyColumn(
        modifier = Modifier.fillMaxSize(),
        contentPadding = PaddingValues(16.dp),
        verticalArrangement = Arrangement.spacedBy(8.dp)
    ) {
        items(rows) { r ->
            Card(modifier = Modifier.fillMaxWidth()) {
                Column(modifier = Modifier.padding(12.dp)) {
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.SpaceBetween,
                        verticalAlignment = Alignment.CenterVertically,
                    ) {
                        Text(
                            r.dest,
                            style = MaterialTheme.typography.titleSmall,
                            fontFamily = FontFamily.Monospace
                        )
                        AssistChip(onClick = {}, label = { Text("${r.hopCount} hop") })
                    }
                    Spacer(Modifier.height(4.dp))
                    Text(
                        "via ${r.nextHop}  tx=${r.viaTransport}  metric=${r.metric}  ttl=${r.expiresInSec}s",
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                        fontFamily = FontFamily.Monospace
                    )
                }
            }
        }
    }
}

@Composable
private fun EmptyMsg(msg: String) {
    Box(modifier = Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
        Text(
            msg,
            style = MaterialTheme.typography.bodyMedium,
            color = MaterialTheme.colorScheme.onSurfaceVariant
        )
    }
}
