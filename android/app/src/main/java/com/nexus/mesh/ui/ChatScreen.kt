package com.nexus.mesh.ui

import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import java.text.SimpleDateFormat
import java.util.*

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun ChatScreen(activity: MainActivity) {
    val service = activity.getService()
    val messages by service?.messages?.collectAsState() ?: remember { mutableStateOf(emptyList()) }

    var destAddr by remember { mutableStateOf("") }
    var messageText by remember { mutableStateOf("") }
    val dateFormat = remember { SimpleDateFormat("HH:mm:ss", Locale.getDefault()) }

    Column(
        modifier = Modifier.fillMaxSize().padding(16.dp)
    ) {
        Text("Messages", style = MaterialTheme.typography.headlineMedium)
        Spacer(Modifier.height(8.dp))

        // Message list
        LazyColumn(
            modifier = Modifier.weight(1f).fillMaxWidth(),
            reverseLayout = true
        ) {
            items(messages.reversed()) { msg ->
                Card(
                    modifier = Modifier.fillMaxWidth().padding(vertical = 4.dp)
                ) {
                    Column(modifier = Modifier.padding(12.dp)) {
                        Row(
                            modifier = Modifier.fillMaxWidth(),
                            horizontalArrangement = Arrangement.SpaceBetween
                        ) {
                            Text(
                                msg.src,
                                style = MaterialTheme.typography.labelMedium,
                                color = MaterialTheme.colorScheme.primary
                            )
                            Text(
                                dateFormat.format(Date(msg.timestamp)),
                                style = MaterialTheme.typography.labelSmall
                            )
                        }
                        Spacer(Modifier.height(4.dp))
                        Text(
                            String(msg.data, Charsets.UTF_8),
                            style = MaterialTheme.typography.bodyMedium
                        )
                    }
                }
            }
        }

        Spacer(Modifier.height(8.dp))
        HorizontalDivider()
        Spacer(Modifier.height(8.dp))

        // Send controls
        OutlinedTextField(
            value = destAddr,
            onValueChange = { destAddr = it.uppercase().take(8) },
            label = { Text("Destination (8 hex chars)") },
            modifier = Modifier.fillMaxWidth(),
            singleLine = true
        )
        Spacer(Modifier.height(4.dp))

        Row(
            modifier = Modifier.fillMaxWidth(),
            verticalAlignment = Alignment.CenterVertically
        ) {
            OutlinedTextField(
                value = messageText,
                onValueChange = { messageText = it },
                label = { Text("Message") },
                modifier = Modifier.weight(1f),
                singleLine = true
            )
            Spacer(Modifier.width(8.dp))
            Button(
                onClick = {
                    if (destAddr.length == 8 && messageText.isNotBlank()) {
                        service?.sendMessage(destAddr, messageText)
                        messageText = ""
                    }
                },
                enabled = destAddr.length == 8 && messageText.isNotBlank()
            ) {
                Text("Send")
            }
        }
    }
}
