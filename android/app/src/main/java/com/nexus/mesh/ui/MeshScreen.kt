package com.nexus.mesh.ui

import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp

@Composable
fun MeshScreen(activity: MainActivity) {
    val service = activity.getService()
    val address by service?.address?.collectAsState() ?: remember { mutableStateOf("--------") }
    val neighbors by service?.neighbors?.collectAsState() ?: remember { mutableStateOf(emptyList()) }

    Column(
        modifier = Modifier.fillMaxSize().padding(16.dp)
    ) {
        Text("Mesh Status", style = MaterialTheme.typography.headlineMedium)
        Spacer(Modifier.height(16.dp))

        // Node info card
        Card(modifier = Modifier.fillMaxWidth()) {
            Column(modifier = Modifier.padding(16.dp)) {
                Text("This Node", style = MaterialTheme.typography.titleMedium)
                Spacer(Modifier.height(8.dp))
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.SpaceBetween
                ) {
                    Text("Address")
                    Text(address, color = MaterialTheme.colorScheme.primary)
                }
            }
        }

        Spacer(Modifier.height(16.dp))
        Text("Neighbors (${neighbors.size})", style = MaterialTheme.typography.titleMedium)
        Spacer(Modifier.height(8.dp))

        if (neighbors.isEmpty()) {
            Text(
                "No neighbors discovered yet.\nOther NEXUS nodes will appear here.",
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
        } else {
            LazyColumn {
                items(neighbors) { neighbor ->
                    Card(
                        modifier = Modifier.fillMaxWidth().padding(vertical = 4.dp)
                    ) {
                        Row(
                            modifier = Modifier.padding(16.dp).fillMaxWidth(),
                            horizontalArrangement = Arrangement.SpaceBetween
                        ) {
                            Text(neighbor.addr, color = MaterialTheme.colorScheme.primary)
                            Text(
                                when (neighbor.role) {
                                    0 -> "LEAF"
                                    1 -> "RELAY"
                                    2 -> "ANCHOR"
                                    3 -> "GATEWAY"
                                    else -> "ROLE=${neighbor.role}"
                                }
                            )
                        }
                    }
                }
            }
        }
    }
}
