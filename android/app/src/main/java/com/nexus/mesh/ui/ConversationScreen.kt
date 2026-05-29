package com.nexus.mesh.ui

import android.content.Intent
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.media.MediaPlayer
import android.media.MediaRecorder
import android.net.Uri
import android.os.Build
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.Image
import androidx.compose.foundation.ExperimentalFoundationApi
import androidx.compose.foundation.background
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
import androidx.compose.material.icons.filled.Add
import androidx.compose.material.icons.filled.Delete
import androidx.compose.material.icons.filled.Edit
import androidx.compose.material.icons.filled.Info
import androidx.compose.material.icons.filled.Mic
import androidx.compose.material.icons.filled.MoreVert
import androidx.compose.material.icons.filled.PlayArrow
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import androidx.core.content.FileProvider
import androidx.navigation.NavController
import androidx.compose.animation.core.RepeatMode
import androidx.compose.animation.core.animateFloat
import androidx.compose.animation.core.infiniteRepeatable
import androidx.compose.animation.core.rememberInfiniteTransition
import androidx.compose.animation.core.tween
import com.nexus.mesh.data.ContactTrust
import com.nexus.mesh.data.DeliveryStatus
import com.nexus.mesh.data.MessageEntity
import com.nexus.mesh.data.MessageType
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.io.ByteArrayOutputStream
import java.io.File
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
    val messages by service?.getMessages(peerAddr)?.collectAsState(initial = emptyList())
        ?: remember { mutableStateOf(emptyList()) }
    val allConversations by service?.getConversations()?.collectAsState(initial = emptyList())
        ?: remember { mutableStateOf(emptyList()) }
    val conv = allConversations.find { it.peerAddr == peerAddr }
    val nickname = conv?.nickname
    val displayName = nickname ?: peerAddr

    val context = LocalContext.current
    val coScope = rememberCoroutineScope()

    var messageText by remember { mutableStateOf("") }
    var sendError by remember { mutableStateOf(false) }
    var showAttachMenu by remember { mutableStateOf(false) }
    var showVoiceRecorder by remember { mutableStateOf(false) }
    var attachError by remember { mutableStateOf<String?>(null) }
    var showNicknameDialog by remember { mutableStateOf(false) }

    val imagePicker = rememberLauncherForActivityResult(
        ActivityResultContracts.GetContent()
    ) { uri ->
        if (uri != null) {
            coScope.launch {
                val result = withContext(Dispatchers.IO) {
                    loadAndDownscaleImage(context, uri)
                }
                if (result == null) {
                    attachError = "Couldn't read or decode image"
                } else {
                    val (bytes, name) = result
                    val ok = service?.sendImage(peerAddr, name, bytes)
                    if (ok != true) attachError = "Send failed (too big or peer unreachable)"
                }
            }
        }
    }
    val filePicker = rememberLauncherForActivityResult(
        ActivityResultContracts.GetContent()
    ) { uri ->
        if (uri != null) {
            coScope.launch {
                val result = withContext(Dispatchers.IO) { loadFile(context, uri) }
                if (result == null) {
                    attachError = "Couldn't read file"
                } else {
                    val (bytes, name, mime) = result
                    if (bytes.size > 3500) {
                        attachError = "File too big (${bytes.size}B, max ~3.5KB)"
                    } else {
                        val ok = service?.sendFile(peerAddr, name, mime, bytes)
                        if (ok != true) attachError = "Send failed"
                    }
                }
            }
        }
    }
    var showRouteInfo by remember { mutableStateOf(false) }
    var showMenuExpanded by remember { mutableStateOf(false) }
    var showClearConfirm by remember { mutableStateOf(false) }
    var selectedMessage by remember { mutableStateOf<MessageEntity?>(null) }
    var reactionTarget by remember { mutableStateOf<MessageEntity?>(null) }

    // Typing indicator
    val typingPeers by service?.typingPeers?.collectAsState() ?: remember { mutableStateOf(emptyMap()) }
    val peerTyping = remember(typingPeers) {
        val ts = typingPeers[peerAddr] ?: 0L
        System.currentTimeMillis() - ts < 4_000L && ts > 0L
    }

    // Trust level badge from contacts
    val allContacts by service?.repository?.getContacts()?.collectAsState(initial = emptyList())
        ?: remember { mutableStateOf(emptyList()) }
    val peerContact = remember(allContacts, peerAddr) { allContacts.find { it.address == peerAddr } }

    // Debounce typing notification: send TYPING NXM when user types
    var lastTypingSentMs by remember { mutableStateOf(0L) }
    var editNickname by remember(nickname) { mutableStateOf(nickname ?: "") }
    val dateFormat = remember { SimpleDateFormat("HH:mm", Locale.getDefault()) }
    val fullDateFormat = remember { SimpleDateFormat("yyyy-MM-dd HH:mm:ss", Locale.getDefault()) }
    val listState = rememberLazyListState()

    LaunchedEffect(messages.size) {
        if (messages.isNotEmpty()) {
            listState.animateScrollToItem(messages.size - 1)
        }
    }

    // Send READ receipts for incoming messages when the conversation is opened
    // or when new messages arrive while the user is viewing it.
    LaunchedEffect(peerAddr, messages.size) {
        service?.sendReadReceipts(peerAddr)
    }

    Scaffold(
        topBar = {
            TopAppBar(
                title = {
                    Column(
                        modifier = Modifier.clickable { showNicknameDialog = true }
                    ) {
                        Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(4.dp)) {
                            Text(displayName, style = MaterialTheme.typography.titleMedium)
                            peerContact?.let { TrustBadge(it.trustLevel) }
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
                                    3 -> "Anchor"; 4 -> "Sentinel"
                                    5 -> "Pillar"; 6 -> "Vault"; else -> "Unknown"
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
                // imePadding() shrinks the visible area while the soft
                // keyboard is open so the input row stays above it and
                // the message bubbles aren't hidden behind it. Without
                // this the LazyColumn extends under the keyboard and
                // recent bubbles disappear.
                .imePadding()
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
                            onLongClick = {
                                selectedMessage = msg
                                reactionTarget = msg
                            }
                        )
                    }
                    if (peerTyping) {
                        item { TypingBubble() }
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
            val err = attachError
            if (err != null) {
                Text(
                    err,
                    color = MaterialTheme.colorScheme.error,
                    style = MaterialTheme.typography.bodySmall,
                    modifier = Modifier
                        .padding(horizontal = 16.dp, vertical = 4.dp)
                        .clickable { attachError = null }
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
                    Box {
                        IconButton(onClick = { showAttachMenu = true }) {
                            Icon(Icons.Default.Add, "Attach")
                        }
                        DropdownMenu(
                            expanded = showAttachMenu,
                            onDismissRequest = { showAttachMenu = false }
                        ) {
                            DropdownMenuItem(
                                text = { Text("Image") },
                                onClick = {
                                    showAttachMenu = false
                                    attachError = null
                                    imagePicker.launch("image/*")
                                }
                            )
                            DropdownMenuItem(
                                text = { Text("File") },
                                onClick = {
                                    showAttachMenu = false
                                    attachError = null
                                    filePicker.launch("*/*")
                                }
                            )
                        }
                    }
                    OutlinedTextField(
                        value = messageText,
                        onValueChange = { v ->
                            messageText = v
                            sendError = false
                            // Debounce typing indicator: send at most once per 2s
                            if (v.isNotEmpty()) {
                                val now = System.currentTimeMillis()
                                if (now - lastTypingSentMs > 2_000L) {
                                    lastTypingSentMs = now
                                    service?.sendTyping(peerAddr)
                                }
                            }
                        },
                        placeholder = { Text("Message") },
                        modifier = Modifier.weight(1f),
                        singleLine = false,
                        maxLines = 4,
                        shape = RoundedCornerShape(24.dp)
                    )
                    Spacer(Modifier.width(4.dp))
                    if (messageText.isBlank()) {
                        IconButton(onClick = {
                            attachError = null
                            showVoiceRecorder = true
                        }) {
                            Icon(Icons.Default.Mic, "Record voice note")
                        }
                    }
                    Spacer(Modifier.width(4.dp))
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
                    InfoRow("Address", peerAddr)

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
                        InfoRow("Next Hop", routeInfo.nextHop)
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
                    if (msg.isOutgoing) {
                        val statusName = when (msg.deliveryStatus) {
                            DeliveryStatus.SENDING -> "Sending"
                            DeliveryStatus.SENT -> "Sent"
                            DeliveryStatus.DELIVERED -> "Delivered"
                            DeliveryStatus.READ -> "Read"
                            DeliveryStatus.FAILED -> "Failed"
                            else -> "Unknown"
                        }
                        InfoRow("Status", statusName)
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

    // Emoji reaction picker
    reactionTarget?.let { target ->
        AlertDialog(
            onDismissRequest = { reactionTarget = null },
            title = { Text("React to message") },
            text = {
                val emojis = listOf("👍", "❤️", "😂", "😮", "😢", "🔥", "✅", "👎")
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.SpaceEvenly
                ) {
                    emojis.forEach { emoji ->
                        Text(
                            emoji,
                            modifier = Modifier
                                .clickable {
                                    val msgId = target.nxmMsgId
                                    if (msgId != null) {
                                        coScope.launch {
                                            service?.sendReaction(peerAddr, emoji, msgId)
                                        }
                                    }
                                    reactionTarget = null
                                }
                                .padding(8.dp),
                            style = MaterialTheme.typography.headlineMedium
                        )
                    }
                }
            },
            confirmButton = {
                TextButton(onClick = { reactionTarget = null }) { Text("Cancel") }
            }
        )
    }

    if (showVoiceRecorder) {
        VoiceRecorderDialog(
            onDismiss = { showVoiceRecorder = false },
            onSend = { data, durSec ->
                showVoiceRecorder = false
                if (data.size > 3500) {
                    attachError = "Voice note too long (${data.size}B). Keep under ~5s."
                } else {
                    val ok = service?.sendVoice(peerAddr, data, durSec)
                    if (ok != true) attachError = "Send failed"
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

@Composable
private fun TypingBubble() {
    val transition = rememberInfiniteTransition(label = "typing")
    val dot1 by transition.animateFloat(
        initialValue = 0.3f, targetValue = 1f,
        animationSpec = infiniteRepeatable(tween(400), RepeatMode.Reverse,
            initialStartOffset = androidx.compose.animation.core.StartOffset(0)),
        label = "d1"
    )
    val dot2 by transition.animateFloat(
        initialValue = 0.3f, targetValue = 1f,
        animationSpec = infiniteRepeatable(tween(400), RepeatMode.Reverse,
            initialStartOffset = androidx.compose.animation.core.StartOffset(133)),
        label = "d2"
    )
    val dot3 by transition.animateFloat(
        initialValue = 0.3f, targetValue = 1f,
        animationSpec = infiniteRepeatable(tween(400), RepeatMode.Reverse,
            initialStartOffset = androidx.compose.animation.core.StartOffset(266)),
        label = "d3"
    )
    Box(modifier = Modifier.fillMaxWidth(), contentAlignment = Alignment.CenterStart) {
        Surface(
            shape = RoundedCornerShape(16.dp, 16.dp, 16.dp, 4.dp),
            color = MaterialTheme.colorScheme.surfaceVariant
        ) {
            Row(
                modifier = Modifier.padding(horizontal = 16.dp, vertical = 10.dp),
                horizontalArrangement = Arrangement.spacedBy(4.dp),
                verticalAlignment = Alignment.CenterVertically
            ) {
                listOf(dot1, dot2, dot3).forEach { alpha ->
                    Box(
                        modifier = Modifier
                            .size(8.dp)
                            .background(
                                MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = alpha),
                                androidx.compose.foundation.shape.CircleShape
                            )
                    )
                }
            }
        }
    }
}

@OptIn(ExperimentalFoundationApi::class)
@Composable
private fun ChatBubble(
    msg: MessageEntity,
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
                MediaAttachment(msg, textColor)
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
                    // Delivery status indicator for outgoing messages
                    if (msg.isOutgoing) {
                        Text(
                            "\u00B7",
                            color = timeColor,
                            style = MaterialTheme.typography.labelSmall
                        )
                        val statusIcon = when (msg.deliveryStatus) {
                            DeliveryStatus.SENDING -> "\u23F3"  // hourglass
                            DeliveryStatus.SENT -> "\u2713"     // single check
                            DeliveryStatus.DELIVERED -> "\u2713\u2713" // double check
                            DeliveryStatus.READ -> "\u2713\u2713"      // double check (blue)
                            DeliveryStatus.FAILED -> "\u2717"   // X
                            else -> ""
                        }
                        val statusColor = if (msg.deliveryStatus == DeliveryStatus.READ)
                            MaterialTheme.colorScheme.inversePrimary
                        else
                            timeColor
                        Text(
                            statusIcon,
                            color = statusColor,
                            style = MaterialTheme.typography.labelSmall
                        )
                    }
                }
                // Reaction chips
                if (msg.reactions.isNotEmpty()) {
                    val counts = msg.reactions.split(",")
                        .filter { it.isNotEmpty() }
                        .groupingBy { it }.eachCount()
                    Spacer(Modifier.height(4.dp))
                    Row(horizontalArrangement = Arrangement.spacedBy(4.dp)) {
                        counts.forEach { (emoji, count) ->
                            Surface(
                                shape = RoundedCornerShape(12.dp),
                                color = MaterialTheme.colorScheme.surface.copy(alpha = 0.85f)
                            ) {
                                Text(
                                    if (count > 1) "$emoji $count" else emoji,
                                    modifier = Modifier.padding(horizontal = 6.dp, vertical = 2.dp),
                                    style = MaterialTheme.typography.labelSmall
                                )
                            }
                        }
                    }
                }
            }
        }
    }
}

/* ─── Media rendering & attachment helpers ──────────────────────────── */

@Composable
private fun MediaAttachment(msg: MessageEntity, tint: androidx.compose.ui.graphics.Color) {
    val path = msg.mediaPath ?: return
    val ctx = LocalContext.current
    when (msg.messageType) {
        MessageType.IMAGE -> {
            val bmp = remember(path) {
                runCatching { BitmapFactory.decodeFile(path) }.getOrNull()
            }
            if (bmp != null) {
                Image(
                    bitmap = bmp.asImageBitmap(),
                    contentDescription = msg.fileName ?: "image",
                    modifier = Modifier
                        .widthIn(max = 260.dp)
                        .heightIn(max = 260.dp)
                        .padding(bottom = 6.dp)
                        .clickable { openFileExternally(ctx, path, msg.mimeType ?: "image/*") }
                )
            }
        }
        MessageType.FILE -> {
            Row(
                verticalAlignment = Alignment.CenterVertically,
                modifier = Modifier
                    .padding(bottom = 6.dp)
                    .clickable { openFileExternally(ctx, path, msg.mimeType ?: "*/*") }
            ) {
                Icon(Icons.Default.Info, contentDescription = "file", tint = tint)
                Spacer(Modifier.width(6.dp))
                Text(
                    msg.fileName ?: "file",
                    color = tint,
                    style = MaterialTheme.typography.bodySmall
                )
            }
        }
        MessageType.VOICE_NOTE -> {
            var playing by remember { mutableStateOf(false) }
            val player = remember { mutableStateOf<MediaPlayer?>(null) }
            DisposableEffect(Unit) {
                onDispose {
                    player.value?.release()
                    player.value = null
                }
            }
            Row(
                verticalAlignment = Alignment.CenterVertically,
                modifier = Modifier
                    .padding(bottom = 6.dp)
                    .clickable {
                        if (playing) {
                            runCatching { player.value?.stop() }
                            runCatching { player.value?.release() }
                            player.value = null
                            playing = false
                        } else {
                            /* Wrap MediaPlayer setup so a missing or
                             * malformed AMR file surfaces as a toast
                             * instead of an uncaught IOException that
                             * silently swallows the user's tap. */
                            val f = File(path)
                            if (!f.exists() || f.length() == 0L) {
                                android.widget.Toast.makeText(
                                    ctx, "Voice file missing (${f.length()}B)",
                                    android.widget.Toast.LENGTH_SHORT
                                ).show()
                                return@clickable
                            }
                            try {
                                val mp = MediaPlayer()
                                mp.setOnErrorListener { _, what, extra ->
                                    android.util.Log.w("voice",
                                        "MediaPlayer error what=$what extra=$extra path=$path size=${f.length()}")
                                    playing = false
                                    runCatching { mp.release() }
                                    if (player.value === mp) player.value = null
                                    android.widget.Toast.makeText(
                                        ctx, "Playback failed (codec $what/$extra)",
                                        android.widget.Toast.LENGTH_SHORT
                                    ).show()
                                    true
                                }
                                mp.setOnCompletionListener {
                                    playing = false
                                    runCatching { it.release() }
                                    if (player.value === it) player.value = null
                                }
                                mp.setDataSource(path)
                                mp.prepare()
                                mp.start()
                                player.value = mp
                                playing = true
                            } catch (e: Exception) {
                                android.util.Log.w("voice",
                                    "MediaPlayer setup failed path=$path size=${f.length()}", e)
                                android.widget.Toast.makeText(
                                    ctx, "Cannot play voice note: ${e.message}",
                                    android.widget.Toast.LENGTH_SHORT
                                ).show()
                            }
                        }
                    }
            ) {
                Icon(
                    Icons.Default.PlayArrow,
                    contentDescription = if (playing) "stop" else "play",
                    tint = tint
                )
                Spacer(Modifier.width(6.dp))
                Text(
                    "${msg.duration}s",
                    color = tint,
                    style = MaterialTheme.typography.bodySmall
                )
            }
        }
    }
}

private fun openFileExternally(ctx: android.content.Context, path: String, mime: String) {
    val file = File(path)
    val uri = FileProvider.getUriForFile(
        ctx, "${ctx.packageName}.fileprovider", file
    )
    val intent = Intent(Intent.ACTION_VIEW).apply {
        setDataAndType(uri, mime)
        addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
    }
    runCatching { ctx.startActivity(intent) }
}

/**
 * Decode an image from a content URI, downscale so the long edge is ≤1600px,
 * recompress as JPEG. With app-layer chunking (see NexusService.sendImage),
 * the on-wire budget is now 1 MB; quality steps down only if encoded size
 * would blow that cap.
 */
private fun loadAndDownscaleImage(
    ctx: android.content.Context,
    uri: Uri
): Pair<ByteArray, String>? {
    val cr = ctx.contentResolver
    val name = queryDisplayName(ctx, uri) ?: "image.jpg"

    // First pass: bounds only
    val opts = BitmapFactory.Options().apply { inJustDecodeBounds = true }
    cr.openInputStream(uri)?.use { BitmapFactory.decodeStream(it, null, opts) }
    val srcW = opts.outWidth
    val srcH = opts.outHeight
    if (srcW <= 0 || srcH <= 0) return null

    val maxEdge = 1600
    var sample = 1
    while (srcW / sample > maxEdge * 2 || srcH / sample > maxEdge * 2) sample *= 2

    val decodeOpts = BitmapFactory.Options().apply { inSampleSize = sample }
    val bmp = cr.openInputStream(uri)?.use {
        BitmapFactory.decodeStream(it, null, decodeOpts)
    } ?: return null

    val scale = minOf(
        maxEdge.toFloat() / bmp.width,
        maxEdge.toFloat() / bmp.height,
        1f
    )
    val scaled = if (scale < 1f) {
        Bitmap.createScaledBitmap(
            bmp,
            (bmp.width * scale).toInt(),
            (bmp.height * scale).toInt(),
            true
        ).also { if (it !== bmp) bmp.recycle() }
    } else bmp

    // Compress at quality 85 by default; step down only if we'd blow the
    // 1 MB cap. App-layer chunking handles wire framing.
    var quality = 85
    val cap = 1024 * 1024  // 1 MB
    var bytes: ByteArray
    while (true) {
        val baos = ByteArrayOutputStream()
        scaled.compress(Bitmap.CompressFormat.JPEG, quality, baos)
        bytes = baos.toByteArray()
        if (bytes.size <= cap || quality <= 30) break
        quality -= 10
    }
    scaled.recycle()
    if (bytes.size > cap) return null
    return bytes to name
}

private fun loadFile(
    ctx: android.content.Context,
    uri: Uri
): Triple<ByteArray, String, String>? {
    val cr = ctx.contentResolver
    val name = queryDisplayName(ctx, uri) ?: "file"
    val mime = cr.getType(uri) ?: "application/octet-stream"
    val bytes = cr.openInputStream(uri)?.use { it.readBytes() } ?: return null
    return Triple(bytes, name, mime)
}

private fun queryDisplayName(ctx: android.content.Context, uri: Uri): String? {
    val cr = ctx.contentResolver
    cr.query(uri, arrayOf(android.provider.OpenableColumns.DISPLAY_NAME),
             null, null, null)?.use { c ->
        if (c.moveToFirst()) {
            val idx = c.getColumnIndex(android.provider.OpenableColumns.DISPLAY_NAME)
            if (idx >= 0) return c.getString(idx)
        }
    }
    return uri.lastPathSegment
}

/* ─── Voice recorder ──────────────────────────────────────────────────── */

@Composable
private fun VoiceRecorderDialog(
    onDismiss: () -> Unit,
    onSend: (ByteArray, Int) -> Unit
) {
    val ctx = LocalContext.current
    var recording by remember { mutableStateOf(false) }
    var elapsed by remember { mutableStateOf(0) }
    var err by remember { mutableStateOf<String?>(null) }
    val fileRef = remember { mutableStateOf<File?>(null) }
    val recorderRef = remember { mutableStateOf<MediaRecorder?>(null) }

    DisposableEffect(Unit) {
        onDispose {
            recorderRef.value?.runCatching { stop() }
            recorderRef.value?.release()
            recorderRef.value = null
        }
    }

    LaunchedEffect(recording) {
        if (recording) {
            while (recording && elapsed < 60) {
                kotlinx.coroutines.delay(1000)
                elapsed++
            }
        }
    }

    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text(if (recording) "Recording…" else "Voice note") },
        text = {
            Column {
                Text(
                    if (recording) "${elapsed}s (tap Stop when done)"
                    else "Tap Record to start. Keep it short — the mesh payload " +
                        "limits voice notes to a few seconds of AMR-NB audio.",
                    style = MaterialTheme.typography.bodyMedium
                )
                err?.let {
                    Spacer(Modifier.height(8.dp))
                    Text(it, color = MaterialTheme.colorScheme.error,
                         style = MaterialTheme.typography.bodySmall)
                }
            }
        },
        confirmButton = {
            if (!recording) {
                TextButton(onClick = {
                    val dir = ctx.cacheDir
                    val f = File(dir, "voice_${System.currentTimeMillis()}.amr")
                    val rec = if (Build.VERSION.SDK_INT >= 31)
                        MediaRecorder(ctx) else @Suppress("DEPRECATION") MediaRecorder()
                    try {
                        rec.setAudioSource(MediaRecorder.AudioSource.MIC)
                        rec.setOutputFormat(MediaRecorder.OutputFormat.AMR_NB)
                        rec.setAudioEncoder(MediaRecorder.AudioEncoder.AMR_NB)
                        rec.setAudioSamplingRate(8000)
                        rec.setAudioEncodingBitRate(4750)
                        rec.setMaxDuration(60_000)
                        rec.setOutputFile(f.absolutePath)
                        /* When the 60s cap is hit, MediaRecorder stops
                         * emitting audio. Auto-stop + auto-send so the
                         * dialog timer can't drift past the actual
                         * recording length. */
                        rec.setOnInfoListener { mr, what, _ ->
                            if (what == MediaRecorder.MEDIA_RECORDER_INFO_MAX_DURATION_REACHED) {
                                try { mr.stop() } catch (_: Exception) {}
                                runCatching { mr.release() }
                                recorderRef.value = null
                                recording = false
                                val captured = f
                                if (captured.exists()) {
                                    val bytes = captured.readBytes()
                                    captured.delete()
                                    if (bytes.isNotEmpty()) onSend(bytes, 60)
                                }
                            }
                        }
                        rec.prepare()
                        rec.start()
                        recorderRef.value = rec
                        fileRef.value = f
                        elapsed = 0
                        recording = true
                        err = null
                    } catch (e: Exception) {
                        err = "Mic unavailable: ${e.message}"
                        rec.runCatching { release() }
                    }
                }) { Text("Record") }
            } else {
                TextButton(onClick = {
                    val rec = recorderRef.value
                    val f = fileRef.value
                    recording = false
                    try { rec?.stop() }
                    catch (e: Exception) { android.util.Log.w("ConversationScreen", "MediaRecorder.stop()", e) }
                    finally { try { rec?.release() } catch (_: Exception) {} }
                    recorderRef.value = null
                    if (f != null && f.exists()) {
                        val bytes = f.readBytes()
                        f.delete()
                        if (bytes.isEmpty()) {
                            err = "Empty recording"
                        } else {
                            onSend(bytes, elapsed.coerceAtLeast(1))
                        }
                    }
                }) { Text("Stop & Send") }
            }
        },
        dismissButton = {
            TextButton(onClick = {
                recorderRef.value?.runCatching { stop() }
                recorderRef.value?.release()
                recorderRef.value = null
                fileRef.value?.delete()
                onDismiss()
            }) { Text("Cancel") }
        }
    )
}
