package com.nexus.mesh.ui

import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.ArrowBack
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import androidx.navigation.NavController
import com.nexus.mesh.data.ContactEntity
import com.nexus.mesh.service.NexusNode
import com.nexus.mesh.service.NexusService
import kotlinx.coroutines.launch
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun AnnounceStreamScreen(activity: MainActivity, navController: NavController) {
    val service = activity.getService()
    val stream by service?.announceStream?.collectAsState()
        ?: remember { mutableStateOf(emptyList()) }
    val contacts by service?.repository?.getContacts()?.collectAsState(initial = emptyList())
        ?: remember { mutableStateOf(emptyList()) }
    val knownAddrs = remember(contacts) {
        contacts.map { it.address }.toHashSet()
    }
    val scope = rememberCoroutineScope()

    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text("Announce Stream") },
                navigationIcon = {
                    IconButton(onClick = { navController.popBackStack() }) {
                        Icon(Icons.Default.ArrowBack, "Back")
                    }
                }
            )
        }
    ) { padding ->
        if (stream.isEmpty()) {
            Box(Modifier.fillMaxSize().padding(padding), contentAlignment = androidx.compose.ui.Alignment.Center) {
                Text(
                    "Waiting for announces...",
                    style = MaterialTheme.typography.bodyMedium,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
            }
        } else {
            LazyColumn(
                modifier = Modifier.fillMaxSize().padding(padding),
                contentPadding = PaddingValues(12.dp),
                verticalArrangement = Arrangement.spacedBy(8.dp)
            ) {
                items(stream, key = { it.addr }) { ev ->
                    AnnounceCard(
                        event = ev,
                        isKnown = ev.addr in knownAddrs,
                        onAddContact = {
                            scope.launch {
                                service?.repository?.upsertContact(
                                    ContactEntity(address = ev.addr)
                                )
                            }
                        },
                        onOpenChat = {
                            navController.navigate("conversation/${ev.addr}")
                        }
                    )
                }
            }
        }
    }
}

@Composable
private fun AnnounceCard(
    event: NexusService.AnnounceEvent,
    isKnown: Boolean,
    onAddContact: () -> Unit,
    onOpenChat: () -> Unit
) {
    val tfmt = remember { SimpleDateFormat("HH:mm:ss", Locale.getDefault()) }
    Card(modifier = Modifier.fillMaxWidth().clickable(onClick = onOpenChat)) {
        Column(Modifier.padding(12.dp)) {
            Row(
                Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = androidx.compose.ui.Alignment.CenterVertically
            ) {
                Text(event.addr, style = MaterialTheme.typography.titleMedium)
                Text(roleName(event.role), style = MaterialTheme.typography.labelMedium,
                     color = MaterialTheme.colorScheme.primary)
            }
            Spacer(Modifier.height(4.dp))
            Text(
                "last ${tfmt.format(Date(event.lastSeenMs))} • ${event.count}× seen",
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
            if (!isKnown) {
                Spacer(Modifier.height(8.dp))
                TextButton(onClick = onAddContact) {
                    Text("Add as contact")
                }
            }
        }
    }
}

private fun roleName(role: Int): String = when (role) {
    NexusNode.ROLE_LEAF -> "LEAF"
    NexusNode.ROLE_RELAY -> "RELAY"
    NexusNode.ROLE_GATEWAY -> "GATEWAY"
    NexusNode.ROLE_ANCHOR -> "ANCHOR"
    NexusNode.ROLE_SENTINEL -> "SENTINEL"
    NexusNode.ROLE_PILLAR -> "PILLAR"
    NexusNode.ROLE_VAULT -> "VAULT"
    else -> "role=$role"
}
