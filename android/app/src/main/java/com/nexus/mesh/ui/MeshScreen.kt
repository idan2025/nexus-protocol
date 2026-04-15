package com.nexus.mesh.ui

import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.navigation.NavController

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun MeshScreen(activity: MainActivity, navController: NavController) {
    val service = activity.getService()
    val address by service?.address?.collectAsState() ?: remember { mutableStateOf("--------") }
    val neighbors by service?.neighbors?.collectAsState() ?: remember { mutableStateOf(emptyList()) }
    val allConversations by service?.getConversations()?.collectAsState(initial = emptyList())
        ?: remember { mutableStateOf(emptyList()) }
    val nicknames = remember(allConversations) {
        allConversations.filter { it.nickname != null }
            .associate { it.peerAddr to it.nickname!! }
    }
    val myName by service?.myName?.collectAsState() ?: remember { mutableStateOf("") }

    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text("Mesh Network") },
                actions = {
                    TextButton(onClick = { navController.navigate("contacts") }) {
                        Text("Contacts")
                    }
                    TextButton(onClick = { navController.navigate("announce_stream") }) {
                        Text("Announces")
                    }
                }
            )
        }
    ) { padding ->
        LazyColumn(
            modifier = Modifier
                .fillMaxSize()
                .padding(padding),
            contentPadding = PaddingValues(16.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            // Node info card
            item {
                Card(modifier = Modifier.fillMaxWidth()) {
                    Column(modifier = Modifier.padding(16.dp)) {
                        Text("This Node", style = MaterialTheme.typography.titleMedium)
                        Spacer(Modifier.height(8.dp))
                        if (myName.isNotEmpty()) {
                            Row(
                                modifier = Modifier.fillMaxWidth(),
                                horizontalArrangement = Arrangement.SpaceBetween
                            ) {
                                Text("Name")
                                Text(myName, color = MaterialTheme.colorScheme.primary)
                            }
                            Spacer(Modifier.height(4.dp))
                        }
                        Row(
                            modifier = Modifier.fillMaxWidth(),
                            horizontalArrangement = Arrangement.SpaceBetween
                        ) {
                            Text("Address")
                            Text(address, color = MaterialTheme.colorScheme.primary)
                        }
                    }
                }
            }

            // Neighbors header
            item {
                Text(
                    "Neighbors (${neighbors.size})",
                    style = MaterialTheme.typography.titleMedium
                )
            }

            if (neighbors.isEmpty()) {
                item {
                    Card(modifier = Modifier.fillMaxWidth()) {
                        Column(modifier = Modifier.padding(16.dp)) {
                            Text(
                                "No neighbors discovered yet.",
                                style = MaterialTheme.typography.bodyMedium,
                                color = MaterialTheme.colorScheme.onSurfaceVariant
                            )
                            Spacer(Modifier.height(4.dp))
                            Text(
                                "Other NEXUS nodes on the same network will appear here automatically.",
                                style = MaterialTheme.typography.bodySmall,
                                color = MaterialTheme.colorScheme.onSurfaceVariant
                            )
                        }
                    }
                }
            } else {
                items(neighbors) { neighbor ->
                    val roleName = when (neighbor.role) {
                        0 -> "LEAF"
                        1 -> "RELAY"
                        2 -> "GATEWAY"
                        3 -> "ANCHOR"
                        4 -> "SENTINEL"
                        5 -> "PILLAR"
                        6 -> "VAULT"
                        else -> "ROLE=${neighbor.role}"
                    }
                    val nickname = nicknames[neighbor.addr]
                    val displayName = nickname ?: neighbor.addr
                    val initials = if (nickname != null) {
                        nickname.take(2).uppercase()
                    } else {
                        neighbor.addr.take(2)
                    }

                    Card(
                        modifier = Modifier
                            .fillMaxWidth()
                            .clickable {
                                navController.navigate("conversation/${neighbor.addr}")
                            }
                    ) {
                        Row(
                            modifier = Modifier
                                .fillMaxWidth()
                                .padding(16.dp),
                            verticalAlignment = Alignment.CenterVertically
                        ) {
                            // Avatar
                            Surface(
                                modifier = Modifier.size(40.dp),
                                shape = MaterialTheme.shapes.extraLarge,
                                color = MaterialTheme.colorScheme.secondaryContainer
                            ) {
                                Box(contentAlignment = Alignment.Center) {
                                    Text(
                                        initials,
                                        style = MaterialTheme.typography.titleSmall,
                                        color = MaterialTheme.colorScheme.onSecondaryContainer
                                    )
                                }
                            }

                            Spacer(Modifier.width(12.dp))

                            Column(modifier = Modifier.weight(1f)) {
                                Text(
                                    displayName,
                                    style = MaterialTheme.typography.titleSmall,
                                    color = MaterialTheme.colorScheme.primary,
                                    maxLines = 1,
                                    overflow = TextOverflow.Ellipsis
                                )
                                if (nickname != null) {
                                    Text(
                                        neighbor.addr,
                                        style = MaterialTheme.typography.labelSmall,
                                        color = MaterialTheme.colorScheme.onSurfaceVariant
                                    )
                                } else {
                                    Text(
                                        "Tap to chat",
                                        style = MaterialTheme.typography.bodySmall,
                                        color = MaterialTheme.colorScheme.onSurfaceVariant
                                    )
                                }
                            }

                            Surface(
                                shape = MaterialTheme.shapes.small,
                                color = MaterialTheme.colorScheme.tertiaryContainer
                            ) {
                                Text(
                                    roleName,
                                    modifier = Modifier.padding(horizontal = 8.dp, vertical = 4.dp),
                                    style = MaterialTheme.typography.labelSmall,
                                    color = MaterialTheme.colorScheme.onTertiaryContainer
                                )
                            }
                        }
                    }
                }
            }
        }
    }
}
