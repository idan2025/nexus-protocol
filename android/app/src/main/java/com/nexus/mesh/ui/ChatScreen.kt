package com.nexus.mesh.ui

import androidx.compose.foundation.ExperimentalFoundationApi
import androidx.compose.foundation.combinedClickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Add
import androidx.compose.material.icons.filled.Delete
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.navigation.NavController
import java.text.SimpleDateFormat
import java.util.*

@OptIn(ExperimentalMaterial3Api::class, ExperimentalFoundationApi::class)
@Composable
fun ChatScreen(activity: MainActivity, navController: NavController) {
    val service = activity.getService()
    val conversations by service?.conversations?.collectAsState()
        ?: remember { mutableStateOf(emptyMap()) }
    val address by service?.address?.collectAsState()
        ?: remember { mutableStateOf("--------") }
    val nicknames by service?.nicknames?.collectAsState()
        ?: remember { mutableStateOf(emptyMap()) }
    val myName by service?.myName?.collectAsState()
        ?: remember { mutableStateOf("") }

    var showNewChat by remember { mutableStateOf(false) }
    var newChatAddr by remember { mutableStateOf("") }
    var deleteTarget by remember { mutableStateOf<String?>(null) }
    val dateFormat = remember { SimpleDateFormat("HH:mm", Locale.getDefault()) }

    val sortedPeers = remember(conversations) {
        conversations.entries
            .filter { it.value.isNotEmpty() }
            .sortedByDescending { it.value.lastOrNull()?.timestamp ?: 0L }
            .map { it.key }
    }

    Scaffold(
        topBar = {
            TopAppBar(
                title = {
                    Column {
                        Text(if (myName.isNotEmpty()) myName else "NEXUS Chat")
                        Text(
                            address,
                            style = MaterialTheme.typography.bodySmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                    }
                }
            )
        },
        floatingActionButton = {
            FloatingActionButton(onClick = { showNewChat = true }) {
                Icon(Icons.Default.Add, contentDescription = "New Chat")
            }
        }
    ) { padding ->
        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(padding)
        ) {
            if (sortedPeers.isEmpty()) {
                Box(
                    modifier = Modifier.fillMaxSize(),
                    contentAlignment = Alignment.Center
                ) {
                    Column(horizontalAlignment = Alignment.CenterHorizontally) {
                        Text(
                            "No conversations yet",
                            style = MaterialTheme.typography.titleMedium,
                            color = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                        Spacer(Modifier.height(8.dp))
                        Text(
                            "Tap + to start a new chat, or\ntap a neighbor on the Mesh tab",
                            style = MaterialTheme.typography.bodyMedium,
                            color = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                    }
                }
            } else {
                LazyColumn(
                    modifier = Modifier.fillMaxSize(),
                    contentPadding = PaddingValues(horizontal = 16.dp, vertical = 8.dp)
                ) {
                    items(sortedPeers) { peerAddr ->
                        val messages = conversations[peerAddr] ?: emptyList()
                        val lastMsg = messages.lastOrNull()
                        val nickname = nicknames[peerAddr]
                        val displayName = nickname ?: peerAddr
                        val initials = if (nickname != null) {
                            nickname.take(2).uppercase()
                        } else {
                            peerAddr.take(2)
                        }
                        val msgCount = messages.size

                        Card(
                            modifier = Modifier
                                .fillMaxWidth()
                                .padding(vertical = 4.dp)
                                .combinedClickable(
                                    onClick = {
                                        navController.navigate("conversation/$peerAddr")
                                    },
                                    onLongClick = {
                                        deleteTarget = peerAddr
                                    }
                                )
                        ) {
                            Row(
                                modifier = Modifier
                                    .fillMaxWidth()
                                    .padding(16.dp),
                                verticalAlignment = Alignment.CenterVertically
                            ) {
                                Surface(
                                    modifier = Modifier.size(48.dp),
                                    shape = MaterialTheme.shapes.extraLarge,
                                    color = MaterialTheme.colorScheme.primaryContainer
                                ) {
                                    Box(contentAlignment = Alignment.Center) {
                                        Text(
                                            initials,
                                            style = MaterialTheme.typography.titleMedium,
                                            color = MaterialTheme.colorScheme.onPrimaryContainer
                                        )
                                    }
                                }

                                Spacer(Modifier.width(12.dp))

                                Column(modifier = Modifier.weight(1f)) {
                                    Row(
                                        modifier = Modifier.fillMaxWidth(),
                                        horizontalArrangement = Arrangement.SpaceBetween
                                    ) {
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
                                                    peerAddr,
                                                    style = MaterialTheme.typography.labelSmall,
                                                    color = MaterialTheme.colorScheme.onSurfaceVariant
                                                )
                                            }
                                        }
                                        Column(horizontalAlignment = Alignment.End) {
                                            if (lastMsg != null) {
                                                Text(
                                                    dateFormat.format(Date(lastMsg.timestamp)),
                                                    style = MaterialTheme.typography.labelSmall,
                                                    color = MaterialTheme.colorScheme.onSurfaceVariant
                                                )
                                            }
                                            Text(
                                                "$msgCount msgs",
                                                style = MaterialTheme.typography.labelSmall,
                                                color = MaterialTheme.colorScheme.onSurfaceVariant
                                            )
                                        }
                                    }
                                    Spacer(Modifier.height(4.dp))
                                    if (lastMsg != null) {
                                        Text(
                                            (if (lastMsg.isOutgoing) "You: " else "") + lastMsg.text,
                                            style = MaterialTheme.typography.bodyMedium,
                                            color = MaterialTheme.colorScheme.onSurfaceVariant,
                                            maxLines = 1,
                                            overflow = TextOverflow.Ellipsis
                                        )
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // New chat dialog
    if (showNewChat) {
        AlertDialog(
            onDismissRequest = { showNewChat = false; newChatAddr = "" },
            title = { Text("New Conversation") },
            text = {
                OutlinedTextField(
                    value = newChatAddr,
                    onValueChange = { newChatAddr = it.uppercase().filter { c -> c in "0123456789ABCDEF" }.take(8) },
                    label = { Text("Node Address (8 hex chars)") },
                    singleLine = true,
                    modifier = Modifier.fillMaxWidth()
                )
            },
            confirmButton = {
                Button(
                    onClick = {
                        if (newChatAddr.length == 8) {
                            showNewChat = false
                            navController.navigate("conversation/$newChatAddr")
                            newChatAddr = ""
                        }
                    },
                    enabled = newChatAddr.length == 8
                ) {
                    Text("Chat")
                }
            },
            dismissButton = {
                TextButton(onClick = { showNewChat = false; newChatAddr = "" }) {
                    Text("Cancel")
                }
            }
        )
    }

    // Delete conversation dialog
    if (deleteTarget != null) {
        val targetAddr = deleteTarget!!
        val targetName = nicknames[targetAddr] ?: targetAddr
        AlertDialog(
            onDismissRequest = { deleteTarget = null },
            icon = { Icon(Icons.Default.Delete, contentDescription = null) },
            title = { Text("Delete Conversation") },
            text = {
                Text("Delete conversation with $targetName?\nThis cannot be undone.")
            },
            confirmButton = {
                Button(
                    onClick = {
                        service?.deleteConversation(targetAddr)
                        deleteTarget = null
                    },
                    colors = ButtonDefaults.buttonColors(
                        containerColor = MaterialTheme.colorScheme.error
                    )
                ) {
                    Text("Delete")
                }
            },
            dismissButton = {
                TextButton(onClick = { deleteTarget = null }) {
                    Text("Cancel")
                }
            }
        )
    }
}
