package com.nexus.mesh.ui

import androidx.compose.foundation.ExperimentalFoundationApi
import androidx.compose.foundation.clickable
import androidx.compose.foundation.combinedClickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.automirrored.filled.Send
import androidx.compose.material.icons.filled.Delete
import androidx.compose.material.icons.filled.Edit
import androidx.compose.material.icons.filled.Info
import androidx.compose.material.icons.filled.MoreVert
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import androidx.navigation.NavController
import java.text.SimpleDateFormat
import java.util.*

@OptIn(ExperimentalMaterial3Api::class, ExperimentalFoundationApi::class)
@Composable
fun ConversationScreen(
    activity: MainActivity,
    navController: NavController,
    peerAddr: String
) {
    val service = activity.getService()
    val conversations by service?.conversations?.collectAsState()
        ?: remember { mutableStateOf(emptyMap()) }
    val nicknames by service?.nicknames?.collectAsState()
        ?: remember { mutableStateOf(emptyMap()) }
    val messages = conversations[peerAddr] ?: emptyList()
    val nickname = nicknames[peerAddr]
    val displayName = nickname ?: peerAddr

    var messageText by remember { mutableStateOf("") }
    var sendError by remember { mutableStateOf(false) }
    var showNicknameDialog by remember { mutableStateOf(false) }
    var showRouteInfo by remember { mutableStateOf(false) }
    var showMenuExpanded by remember { mutableStateOf(false) }
    var showClearConfirm by remember { mutableStateOf(false) }
    var selectedMessage by remember { mutableStateOf<com.nexus.mesh.service.NexusService.ChatMessage?>(null) }
    var editNickname by remember(nickname) { mutableStateOf(nickname ?: "") }
    val dateFormat = remember { SimpleDateFormat("HH:mm", Locale.getDefault()) }
    val fullDateFormat = remember { SimpleDateFormat("yyyy-MM-dd HH:mm:ss", Locale.getDefault()) }
    val listState = rememberLazyListState()

    LaunchedEffect(messages.size) {
        if (messages.isNotEmpty()) {
            listState.animateScrollToItem(messages.size - 1)
        }
    }

    Scaffold(
        topBar = {
            TopAppBar(
                title = {
                    Column(
                        modifier = Modifier.clickable { showNicknameDialog = true }
                    ) {
                        Row(verticalAlignment = Alignment.CenterVertically) {
                            Text(displayName, style = MaterialTheme.typography.titleMedium)
                            Spacer(Modifier.width(4.dp))
                            Icon(
                                Icons.Default.Edit,
                                contentDescription = "Edit name",
                                modifier = Modifier.size(14.dp),
                                tint = MaterialTheme.colorScheme.onSurfaceVariant
                            )
                        }
                        if (nickname != null) {
                            Text(
                                peerAddr,
                                style = MaterialTheme.typography.bodySmall,
                                color = MaterialTheme.colorScheme.onSurfaceVariant
                            )
                        } else {
                            val neighbors by service?.neighbors?.collectAsState()
                                ?: remember { mutableStateOf(emptyList()) }
                            val neighbor = neighbors.find { it.addr == peerAddr }
                            if (neighbor != null) {
                                val roleName = when (neighbor.role) {
                                    0 -> "Leaf"; 1 -> "Relay"; 2 -> "Gateway"
                                    3 -> "Anchor"; 4 -> "Sentinel"; else -> "Unknown"
                                }
                                Text(
                                    "Direct neighbor ($roleName)",
                                    style = MaterialTheme.typography.bodySmall,
                                    color = MaterialTheme.colorScheme.onSurfaceVariant
                                )
                            } else {
                                Text(
                                    "Tap to set nickname",
                                    style = MaterialTheme.typography.bodySmall,
                                    color = MaterialTheme.colorScheme.onSurfaceVariant
                                )
                            }
                        }
                    }
                },
                navigationIcon = {
                    IconButton(onClick = { navController.popBackStack() }) {
                        Icon(Icons.AutoMirrored.Filled.ArrowBack, "Back")
                    }
                },
                actions = {
                    IconButton(onClick = { showRouteInfo = true }) {
                        Icon(Icons.Default.Info, "Route Info")
                    }
                    Box {
                        IconButton(onClick = { showMenuExpanded = true }) {
                            Icon(Icons.Default.MoreVert, "More")
                        }
                        DropdownMenu(
                            expanded = showMenuExpanded,
                            onDismissRequest = { showMenuExpanded = false }
                        ) {
                            DropdownMenuItem(
                                text = { Text("Set Nickname") },
                                leadingIcon = { Icon(Icons.Default.Edit, null) },
                                onClick = {
                                    showMenuExpanded = false
                                    showNicknameDialog = true
                                }
                            )
                            DropdownMenuItem(
                                text = { Text("Route Info") },
                                leadingIcon = { Icon(Icons.Default.Info, null) },
                                onClick = {
                                    showMenuExpanded = false
                                    showRouteInfo = true
                                }
                            )
                            Divider()
                            DropdownMenuItem(
                                text = { Text("Clear Messages") },
                                leadingIcon = {
                                    Icon(Icons.Default.Delete, null,
                                        tint = MaterialTheme.colorScheme.error)
                                },
                                onClick = {
                                    showMenuExpanded = false
                                    showClearConfirm = true
                                }
                            )
                            DropdownMenuItem(
                                text = {
                                    Text("Delete Conversation",
                                        color = MaterialTheme.colorScheme.error)
                                },
                                leadingIcon = {
                                    Icon(Icons.Default.Delete, null,
                                        tint = MaterialTheme.colorScheme.error)
                                },
                                onClick = {
                                    showMenuExpanded = false
                                    service?.deleteConversation(peerAddr)
                                    navController.popBackStack()
                                }
                            )
                        }
                    }
                }
            )
        }
    ) { padding ->
        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(padding)
        ) {
            if (messages.isEmpty()) {
                Box(
                    modifier = Modifier
                        .weight(1f)
                        .fillMaxWidth(),
                    contentAlignment = Alignment.Center
                ) {
                    Text(
                        "No messages yet.\nSend the first message!",
                        style = MaterialTheme.typography.bodyMedium,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                }
            } else {
                LazyColumn(
                    modifier = Modifier
                        .weight(1f)
                        .fillMaxWidth(),
                    state = listState,
                    contentPadding = PaddingValues(horizontal = 16.dp, vertical = 8.dp),
                    verticalArrangement = Arrangement.spacedBy(4.dp)
                ) {
                    items(messages) { msg ->
                        ChatBubble(
                            msg = msg,
                            dateFormat = dateFormat,
                            onLongClick = { selectedMessage = msg }
                        )
                    }
                }
            }

            if (sendError) {
                Text(
                    "Send failed. Is the peer reachable?",
                    color = MaterialTheme.colorScheme.error,
                    style = MaterialTheme.typography.bodySmall,
                    modifier = Modifier.padding(horizontal = 16.dp, vertical = 4.dp)
                )
            }

            Surface(
                tonalElevation = 3.dp,
                modifier = Modifier.fillMaxWidth()
            ) {
                Row(
                    modifier = Modifier
                        .fillMaxWidth()
                        .padding(horizontal = 12.dp, vertical = 8.dp),
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    OutlinedTextField(
                        value = messageText,
                        onValueChange = {
                            messageText = it
                            sendError = false
                        },
                        placeholder = { Text("Message") },
                        modifier = Modifier.weight(1f),
                        singleLine = false,
                        maxLines = 4,
                        shape = RoundedCornerShape(24.dp)
                    )
                    Spacer(Modifier.width(8.dp))
                    FilledIconButton(
                        onClick = {
                            if (messageText.isNotBlank()) {
                                val ok = service?.sendMessage(peerAddr, messageText.trim())
                                if (ok == true) {
                                    messageText = ""
                                    sendError = false
                                } else {
                                    sendError = true
                                }
                            }
                        },
                        enabled = messageText.isNotBlank()
                    ) {
                        Icon(Icons.AutoMirrored.Filled.Send, "Send")
                    }
                }
            }
        }
    }

    // Nickname edit dialog
    if (showNicknameDialog) {
        AlertDialog(
            onDismissRequest = { showNicknameDialog = false },
            title = { Text("Set Nickname") },
            text = {
                Column {
                    Text(
                        "Node: $peerAddr",
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                    Spacer(Modifier.height(12.dp))
                    OutlinedTextField(
                        value = editNickname,
                        onValueChange = { editNickname = it },
                        label = { Text("Nickname") },
                        placeholder = { Text("e.g. Alice, Bob, Home Node") },
                        singleLine = true,
                        modifier = Modifier.fillMaxWidth()
                    )
                }
            },
            confirmButton = {
                Button(onClick = {
                    service?.setNickname(peerAddr, editNickname)
                    showNicknameDialog = false
                }) {
                    Text("Save")
                }
            },
            dismissButton = {
                if (nickname != null) {
                    TextButton(onClick = {
                        service?.setNickname(peerAddr, "")
                        editNickname = ""
                        showNicknameDialog = false
                    }) {
                        Text("Remove")
                    }
                } else {
                    TextButton(onClick = { showNicknameDialog = false }) {
                        Text("Cancel")
                    }
                }
            }
        )
    }

    // Route info dialog
    if (showRouteInfo) {
        val routeInfo = remember(peerAddr) { service?.getRouteInfo(peerAddr) }
        val isNeighbor = remember(peerAddr) { service?.isPeerNeighbor(peerAddr) ?: false }
        val neighbors by service?.neighbors?.collectAsState()
            ?: remember { mutableStateOf(emptyList()) }
        val neighbor = neighbors.find { it.addr == peerAddr }

        AlertDialog(
            onDismissRequest = { showRouteInfo = false },
            icon = { Icon(Icons.Default.Info, null) },
            title = { Text("Route to $displayName") },
            text = {
                Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                    // Address
                    InfoRow("Address", peerAddr)

                    // Connection type
                    if (isNeighbor) {
                        InfoRow("Connection", "Direct neighbor")
                        if (neighbor != null) {
                            val roleName = when (neighbor.role) {
                                0 -> "Leaf"; 1 -> "Relay"; 2 -> "Gateway"
                                3 -> "Anchor"; 4 -> "Sentinel"; else -> "Unknown"
                            }
                            InfoRow("Role", roleName)
                        }
                    } else if (routeInfo != null) {
                        InfoRow("Connection", "Multi-hop")
                        InfoRow("Hops", "${routeInfo.hopCount}")
                        val nextHopName = nicknames[routeInfo.nextHop] ?: routeInfo.nextHop
                        InfoRow("Next Hop", nextHopName)
                        val transportName = when (routeInfo.viaTransport) {
                            0 -> "Default"
                            1 -> "TCP"
                            2 -> "UDP"
                            3 -> "BLE"
                            4 -> "LoRa"
                            else -> "Transport ${routeInfo.viaTransport}"
                        }
                        InfoRow("Via", transportName)
                    } else {
                        InfoRow("Connection", "No route found")
                        Text(
                            "Messages will be flooded across all transports.",
                            style = MaterialTheme.typography.bodySmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                    }

                    Divider()

                    // Stats
                    val msgCount = messages.size
                    val sentCount = messages.count { it.isOutgoing }
                    val recvCount = msgCount - sentCount
                    InfoRow("Messages", "$msgCount total")
                    InfoRow("Sent / Received", "$sentCount / $recvCount")
                }
            },
            confirmButton = {
                TextButton(onClick = { showRouteInfo = false }) {
                    Text("Close")
                }
            }
        )
    }

    // Clear messages confirm dialog
    if (showClearConfirm) {
        AlertDialog(
            onDismissRequest = { showClearConfirm = false },
            icon = { Icon(Icons.Default.Delete, null) },
            title = { Text("Clear Messages") },
            text = { Text("Clear all messages with $displayName?\nThis cannot be undone.") },
            confirmButton = {
                Button(
                    onClick = {
                        service?.clearConversation(peerAddr)
                        showClearConfirm = false
                    },
                    colors = ButtonDefaults.buttonColors(
                        containerColor = MaterialTheme.colorScheme.error
                    )
                ) {
                    Text("Clear")
                }
            },
            dismissButton = {
                TextButton(onClick = { showClearConfirm = false }) {
                    Text("Cancel")
                }
            }
        )
    }

    // Message detail/delete dialog
    if (selectedMessage != null) {
        val msg = selectedMessage!!
        AlertDialog(
            onDismissRequest = { selectedMessage = null },
            title = { Text(if (msg.isOutgoing) "Sent Message" else "Received Message") },
            text = {
                Column(verticalArrangement = Arrangement.spacedBy(6.dp)) {
                    // Message preview
                    Surface(
                        shape = RoundedCornerShape(8.dp),
                        color = MaterialTheme.colorScheme.surfaceVariant,
                        modifier = Modifier.fillMaxWidth()
                    ) {
                        Text(
                            msg.text,
                            modifier = Modifier.padding(12.dp),
                            style = MaterialTheme.typography.bodyMedium,
                            maxLines = 5
                        )
                    }

                    Spacer(Modifier.height(4.dp))

                    InfoRow("Time", fullDateFormat.format(Date(msg.timestamp)))
                    InfoRow("Direction", if (msg.isOutgoing) "Sent" else "Received")
                    if (!msg.isOutgoing) {
                        InfoRow("Source", if (msg.isDirect) "Direct" else "Multi-hop relay")
                    }
                    InfoRow("Size", "${msg.text.toByteArray(Charsets.UTF_8).size} bytes")
                }
            },
            confirmButton = {
                TextButton(onClick = { selectedMessage = null }) {
                    Text("Close")
                }
            },
            dismissButton = {
                TextButton(
                    onClick = {
                        service?.deleteMessage(peerAddr, msg.id)
                        selectedMessage = null
                    },
                    colors = ButtonDefaults.textButtonColors(
                        contentColor = MaterialTheme.colorScheme.error
                    )
                ) {
                    Text("Delete")
                }
            }
        )
    }
}

@Composable
private fun InfoRow(label: String, value: String) {
    Row(
        modifier = Modifier.fillMaxWidth(),
        horizontalArrangement = Arrangement.SpaceBetween
    ) {
        Text(
            label,
            style = MaterialTheme.typography.bodyMedium,
            color = MaterialTheme.colorScheme.onSurfaceVariant
        )
        Text(
            value,
            style = MaterialTheme.typography.bodyMedium,
            color = MaterialTheme.colorScheme.primary
        )
    }
}

@OptIn(ExperimentalFoundationApi::class)
@Composable
private fun ChatBubble(
    msg: com.nexus.mesh.service.NexusService.ChatMessage,
    dateFormat: SimpleDateFormat,
    onLongClick: () -> Unit
) {
    val alignment = if (msg.isOutgoing) Alignment.CenterEnd else Alignment.CenterStart
    val bubbleColor = if (msg.isOutgoing)
        MaterialTheme.colorScheme.primary
    else
        MaterialTheme.colorScheme.surfaceVariant
    val textColor = if (msg.isOutgoing)
        MaterialTheme.colorScheme.onPrimary
    else
        MaterialTheme.colorScheme.onSurfaceVariant
    val timeColor = if (msg.isOutgoing)
        MaterialTheme.colorScheme.onPrimary.copy(alpha = 0.7f)
    else
        MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.7f)
    val bubbleShape = if (msg.isOutgoing)
        RoundedCornerShape(16.dp, 16.dp, 4.dp, 16.dp)
    else
        RoundedCornerShape(16.dp, 16.dp, 16.dp, 4.dp)

    Box(
        modifier = Modifier.fillMaxWidth(),
        contentAlignment = alignment
    ) {
        Surface(
            shape = bubbleShape,
            color = bubbleColor,
            modifier = Modifier
                .widthIn(max = 300.dp)
                .combinedClickable(
                    onClick = {},
                    onLongClick = onLongClick
                )
        ) {
            Column(modifier = Modifier.padding(12.dp)) {
                Text(
                    msg.text,
                    color = textColor,
                    style = MaterialTheme.typography.bodyMedium
                )
                Spacer(Modifier.height(2.dp))
                Row(
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.spacedBy(4.dp)
                ) {
                    // Source indicator for received messages
                    if (!msg.isOutgoing) {
                        Text(
                            if (msg.isDirect) "direct" else "relayed",
                            color = timeColor,
                            style = MaterialTheme.typography.labelSmall
                        )
                        Text(
                            "\u00B7",
                            color = timeColor,
                            style = MaterialTheme.typography.labelSmall
                        )
                    }
                    Text(
                        dateFormat.format(Date(msg.timestamp)),
                        color = timeColor,
                        style = MaterialTheme.typography.labelSmall
                    )
                }
            }
        }
    }
}
