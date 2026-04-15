package com.nexus.mesh.ui

import androidx.compose.foundation.ExperimentalFoundationApi
import androidx.compose.foundation.combinedClickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Add
import androidx.compose.material.icons.filled.Delete
import androidx.compose.material.icons.filled.Search
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.navigation.NavController
import com.nexus.mesh.data.ConversationEntity
import com.nexus.mesh.data.GroupEntity
import java.text.SimpleDateFormat
import java.util.*

@OptIn(ExperimentalMaterial3Api::class, ExperimentalFoundationApi::class)
@Composable
fun ChatScreen(activity: MainActivity, navController: NavController) {
    val service = activity.getService()
    val conversations by service?.getConversations()?.collectAsState(initial = emptyList())
        ?: remember { mutableStateOf(emptyList()) }
    val groups by service?.getGroups()?.collectAsState(initial = emptyList())
        ?: remember { mutableStateOf(emptyList<GroupEntity>()) }
    val address by service?.address?.collectAsState()
        ?: remember { mutableStateOf("--------") }
    val myName by service?.myName?.collectAsState()
        ?: remember { mutableStateOf("") }

    var showNewChat by remember { mutableStateOf(false) }
    var showNewGroup by remember { mutableStateOf(false) }
    var newChatAddr by remember { mutableStateOf("") }
    var newGroupName by remember { mutableStateOf("") }
    var deleteTarget by remember { mutableStateOf<String?>(null) }
    var deleteGroupTarget by remember { mutableStateOf<String?>(null) }
    val dateFormat = remember { SimpleDateFormat("HH:mm", Locale.getDefault()) }

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
                },
                actions = {
                    IconButton(onClick = { navController.navigate("search") }) {
                        Icon(Icons.Default.Search, contentDescription = "Search messages")
                    }
                }
            )
        },
        floatingActionButton = {
            var fabExpanded by remember { mutableStateOf(false) }
            Column(horizontalAlignment = Alignment.End) {
                if (fabExpanded) {
                    SmallFloatingActionButton(
                        onClick = { fabExpanded = false; showNewGroup = true }
                    ) {
                        Text("Group", modifier = Modifier.padding(horizontal = 8.dp),
                            style = MaterialTheme.typography.labelSmall)
                    }
                    Spacer(Modifier.height(8.dp))
                    SmallFloatingActionButton(
                        onClick = { fabExpanded = false; showNewChat = true }
                    ) {
                        Text("Chat", modifier = Modifier.padding(horizontal = 8.dp),
                            style = MaterialTheme.typography.labelSmall)
                    }
                    Spacer(Modifier.height(8.dp))
                }
                FloatingActionButton(onClick = { fabExpanded = !fabExpanded }) {
                    Icon(Icons.Default.Add, contentDescription = "New")
                }
            }
        }
    ) { padding ->
        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(padding)
        ) {
            if (conversations.isEmpty() && groups.isEmpty()) {
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
                            "Tap + to start a new chat or group",
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
                    if (groups.isNotEmpty()) {
                        item {
                            Text(
                                "Groups",
                                style = MaterialTheme.typography.labelMedium,
                                color = MaterialTheme.colorScheme.onSurfaceVariant,
                                modifier = Modifier.padding(vertical = 4.dp)
                            )
                        }
                        items(groups) { group ->
                            GroupConversationItem(
                                group = group,
                                dateFormat = dateFormat,
                                onClick = {
                                    navController.navigate("group_conversation/${group.groupId}")
                                },
                                onLongClick = {
                                    deleteGroupTarget = group.groupId
                                }
                            )
                        }
                        item {
                            Text(
                                "Direct Messages",
                                style = MaterialTheme.typography.labelMedium,
                                color = MaterialTheme.colorScheme.onSurfaceVariant,
                                modifier = Modifier.padding(vertical = 4.dp)
                            )
                        }
                    }
                    items(conversations) { conv ->
                        ConversationItem(
                            conv = conv,
                            dateFormat = dateFormat,
                            onClick = {
                                navController.navigate("conversation/${conv.peerAddr}")
                            },
                            onLongClick = {
                                deleteTarget = conv.peerAddr
                            }
                        )
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

    // New group dialog
    if (showNewGroup) {
        AlertDialog(
            onDismissRequest = { showNewGroup = false; newGroupName = "" },
            title = { Text("Create Group") },
            text = {
                Column {
                    OutlinedTextField(
                        value = newGroupName,
                        onValueChange = { newGroupName = it },
                        label = { Text("Group Name (optional)") },
                        singleLine = true,
                        modifier = Modifier.fillMaxWidth()
                    )
                    Spacer(Modifier.height(8.dp))
                    Text(
                        "A random group ID and key will be generated.",
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                }
            },
            confirmButton = {
                Button(onClick = {
                    // Generate random 4-byte group ID as hex
                    val idBytes = ByteArray(4)
                    java.security.SecureRandom().nextBytes(idBytes)
                    val groupIdHex = idBytes.joinToString("") { "%02X".format(it) }
                    val name = newGroupName.trim().ifEmpty { null }
                    service?.createGroup(groupIdHex, name)
                    showNewGroup = false
                    newGroupName = ""
                }) {
                    Text("Create")
                }
            },
            dismissButton = {
                TextButton(onClick = { showNewGroup = false; newGroupName = "" }) {
                    Text("Cancel")
                }
            }
        )
    }

    // Delete group dialog
    if (deleteGroupTarget != null) {
        val targetId = deleteGroupTarget!!
        val targetGroup = groups.find { it.groupId == targetId }
        val targetName = targetGroup?.name ?: targetId
        AlertDialog(
            onDismissRequest = { deleteGroupTarget = null },
            icon = { Icon(Icons.Default.Delete, contentDescription = null) },
            title = { Text("Delete Group") },
            text = { Text("Delete group $targetName?\nThis cannot be undone.") },
            confirmButton = {
                Button(
                    onClick = {
                        service?.deleteGroup(targetId)
                        deleteGroupTarget = null
                    },
                    colors = ButtonDefaults.buttonColors(
                        containerColor = MaterialTheme.colorScheme.error
                    )
                ) {
                    Text("Delete")
                }
            },
            dismissButton = {
                TextButton(onClick = { deleteGroupTarget = null }) {
                    Text("Cancel")
                }
            }
        )
    }

    // Delete conversation dialog
    if (deleteTarget != null) {
        val targetAddr = deleteTarget!!
        val targetConv = conversations.find { it.peerAddr == targetAddr }
        val targetName = targetConv?.nickname ?: targetAddr
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

@OptIn(ExperimentalFoundationApi::class, ExperimentalMaterial3Api::class)
@Composable
private fun GroupConversationItem(
    group: GroupEntity,
    dateFormat: SimpleDateFormat,
    onClick: () -> Unit,
    onLongClick: () -> Unit
) {
    val displayName = group.name ?: "Group ${group.groupId}"
    val initials = if (group.name != null) group.name.take(2).uppercase()
                   else "G${group.groupId.take(1)}"

    Card(
        modifier = Modifier
            .fillMaxWidth()
            .padding(vertical = 4.dp)
            .combinedClickable(onClick = onClick, onLongClick = onLongClick)
    ) {
        Row(
            modifier = Modifier.fillMaxWidth().padding(16.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            Surface(
                modifier = Modifier.size(48.dp),
                shape = MaterialTheme.shapes.extraLarge,
                color = MaterialTheme.colorScheme.tertiaryContainer
            ) {
                Box(contentAlignment = Alignment.Center) {
                    Text(
                        initials,
                        style = MaterialTheme.typography.titleMedium,
                        color = MaterialTheme.colorScheme.onTertiaryContainer
                    )
                }
            }
            Spacer(Modifier.width(12.dp))
            Column(modifier = Modifier.weight(1f)) {
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.SpaceBetween
                ) {
                    Text(
                        displayName,
                        style = MaterialTheme.typography.titleSmall,
                        color = MaterialTheme.colorScheme.tertiary,
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis,
                        modifier = Modifier.weight(1f)
                    )
                    Column(horizontalAlignment = Alignment.End) {
                        if (group.lastMessageTime > 0) {
                            Text(
                                dateFormat.format(Date(group.lastMessageTime)),
                                style = MaterialTheme.typography.labelSmall,
                                color = MaterialTheme.colorScheme.onSurfaceVariant
                            )
                        }
                        if (group.unreadCount > 0) {
                            Badge { Text("${group.unreadCount}") }
                        }
                    }
                }
                if (group.lastMessagePreview.isNotEmpty()) {
                    Spacer(Modifier.height(4.dp))
                    Text(
                        group.lastMessagePreview,
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

@OptIn(ExperimentalFoundationApi::class, ExperimentalMaterial3Api::class)
@Composable
private fun ConversationItem(
    conv: ConversationEntity,
    dateFormat: SimpleDateFormat,
    onClick: () -> Unit,
    onLongClick: () -> Unit
) {
    val displayName = conv.nickname ?: conv.peerAddr
    val initials = if (conv.nickname != null) {
        conv.nickname.take(2).uppercase()
    } else {
        conv.peerAddr.take(2)
    }

    Card(
        modifier = Modifier
            .fillMaxWidth()
            .padding(vertical = 4.dp)
            .combinedClickable(
                onClick = onClick,
                onLongClick = onLongClick
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
                        if (conv.nickname != null) {
                            Text(
                                conv.peerAddr,
                                style = MaterialTheme.typography.labelSmall,
                                color = MaterialTheme.colorScheme.onSurfaceVariant
                            )
                        }
                    }
                    Column(horizontalAlignment = Alignment.End) {
                        if (conv.lastMessageTime > 0) {
                            Text(
                                dateFormat.format(Date(conv.lastMessageTime)),
                                style = MaterialTheme.typography.labelSmall,
                                color = MaterialTheme.colorScheme.onSurfaceVariant
                            )
                        }
                        if (conv.unreadCount > 0) {
                            Badge {
                                Text("${conv.unreadCount}")
                            }
                        }
                    }
                }
                Spacer(Modifier.height(4.dp))
                if (conv.lastMessagePreview.isNotEmpty()) {
                    Text(
                        conv.lastMessagePreview,
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
