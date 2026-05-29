package com.nexus.mesh.ui

import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.filled.Add
import androidx.compose.material.icons.filled.Check
import androidx.compose.material.icons.filled.Delete
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.input.PasswordVisualTransformation
import androidx.compose.ui.unit.dp
import androidx.navigation.NavController
import com.nexus.mesh.data.IdentityRecord
import com.nexus.mesh.service.IdentityManager

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun IdentityListScreen(activity: MainActivity, navController: NavController) {
    val service = activity.getService()
    val identityManager = service?.identityManager ?: return

    var identities by remember { mutableStateOf(identityManager.getAll()) }
    var activeId by remember { mutableStateOf(identityManager.getActiveId()) }
    var showCreateDialog by remember { mutableStateOf(false) }
    var showImportDialog by remember { mutableStateOf(false) }
    var deleteTarget by remember { mutableStateOf<IdentityRecord?>(null) }
    var statusMsg by remember { mutableStateOf<String?>(null) }

    fun refresh() {
        identities = identityManager.getAll()
        activeId = identityManager.getActiveId()
    }

    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text("Identities") },
                navigationIcon = {
                    IconButton(onClick = { navController.popBackStack() }) {
                        Icon(Icons.AutoMirrored.Filled.ArrowBack, "Back")
                    }
                },
                actions = {
                    IconButton(onClick = { showCreateDialog = true }) {
                        Icon(Icons.Default.Add, "Create identity")
                    }
                }
            )
        }
    ) { padding ->
        Column(modifier = Modifier.padding(padding).fillMaxSize()) {
            if (statusMsg != null) {
                Surface(
                    color = MaterialTheme.colorScheme.primaryContainer,
                    modifier = Modifier.fillMaxWidth()
                ) {
                    Text(
                        statusMsg!!,
                        modifier = Modifier.padding(12.dp),
                        style = MaterialTheme.typography.bodySmall
                    )
                }
            }

            LazyColumn(
                modifier = Modifier.weight(1f),
                contentPadding = PaddingValues(16.dp),
                verticalArrangement = Arrangement.spacedBy(8.dp)
            ) {
                items(identities, key = { it.id }) { rec ->
                    IdentityCard(
                        record = rec,
                        isActive = rec.id == activeId,
                        onSwitch = {
                            service.switchIdentity(rec.id)
                            statusMsg = "Switching to ${rec.name}…"
                            refresh()
                        },
                        onDelete = { deleteTarget = rec }
                    )
                }
                item {
                    OutlinedButton(
                        onClick = { showImportDialog = true },
                        modifier = Modifier.fillMaxWidth()
                    ) { Text("Import Identity Backup") }
                }
            }
        }
    }

    if (showCreateDialog) {
        CreateIdentityDialog(
            onConfirm = { name ->
                val rec = identityManager.createNew(name)
                refresh()
                statusMsg = "Identity '${rec.name}' created. Switch to it to activate."
                showCreateDialog = false
            },
            onDismiss = { showCreateDialog = false }
        )
    }

    if (showImportDialog) {
        ImportIdentityDialog(
            onConfirm = { name, blob, passphrase ->
                try {
                    val rec = identityManager.importIdentity(name, blob, passphrase)
                    refresh()
                    statusMsg = "Imported '${rec.name}'. Switch to activate."
                    showImportDialog = false
                } catch (e: Exception) {
                    statusMsg = "Import failed: ${e.message}"
                }
            },
            onDismiss = { showImportDialog = false }
        )
    }

    deleteTarget?.let { rec ->
        AlertDialog(
            onDismissRequest = { deleteTarget = null },
            title = { Text("Delete Identity?") },
            text = { Text("'${rec.name}' and its conversation history will be permanently removed.") },
            confirmButton = {
                TextButton(onClick = {
                    identityManager.delete(rec.id)
                    refresh()
                    deleteTarget = null
                    statusMsg = "Deleted '${rec.name}'"
                }) { Text("Delete", color = MaterialTheme.colorScheme.error) }
            },
            dismissButton = {
                TextButton(onClick = { deleteTarget = null }) { Text("Cancel") }
            }
        )
    }
}

@Composable
private fun IdentityCard(
    record: IdentityRecord,
    isActive: Boolean,
    onSwitch: () -> Unit,
    onDelete: () -> Unit
) {
    Card(modifier = Modifier.fillMaxWidth()) {
        Row(
            modifier = Modifier.padding(12.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            Column(modifier = Modifier.weight(1f)) {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Text(record.name, style = MaterialTheme.typography.titleSmall)
                    if (isActive) {
                        Spacer(Modifier.width(6.dp))
                        Surface(
                            shape = MaterialTheme.shapes.small,
                            color = MaterialTheme.colorScheme.primaryContainer
                        ) {
                            Text(
                                "Active",
                                modifier = Modifier.padding(horizontal = 6.dp, vertical = 2.dp),
                                style = MaterialTheme.typography.labelSmall,
                                color = MaterialTheme.colorScheme.onPrimaryContainer
                            )
                        }
                    }
                }
                if (record.addrHex.isNotBlank()) {
                    Text(
                        record.addrHex,
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                }
            }
            if (!isActive) {
                IconButton(onClick = onSwitch) {
                    Icon(Icons.Default.Check, "Switch to this identity")
                }
            }
            IconButton(onClick = onDelete, enabled = !isActive) {
                Icon(Icons.Default.Delete, "Delete identity",
                    tint = if (isActive) MaterialTheme.colorScheme.onSurface.copy(alpha = 0.3f)
                           else MaterialTheme.colorScheme.error)
            }
        }
    }
}

@Composable
private fun CreateIdentityDialog(onConfirm: (String) -> Unit, onDismiss: () -> Unit) {
    var name by remember { mutableStateOf("") }
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text("New Identity") },
        text = {
            OutlinedTextField(
                value = name,
                onValueChange = { name = it },
                label = { Text("Label") },
                placeholder = { Text("e.g. Work, Personal") },
                singleLine = true,
                modifier = Modifier.fillMaxWidth()
            )
        },
        confirmButton = {
            TextButton(onClick = { onConfirm(name) }, enabled = name.isNotBlank()) {
                Text("Create")
            }
        },
        dismissButton = { TextButton(onClick = onDismiss) { Text("Cancel") } }
    )
}

@Composable
private fun ImportIdentityDialog(
    onConfirm: (name: String, blob: String, passphrase: CharArray) -> Unit,
    onDismiss: () -> Unit
) {
    var name by remember { mutableStateOf("") }
    var blob by remember { mutableStateOf("") }
    var passphrase by remember { mutableStateOf("") }
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text("Import Identity") },
        text = {
            Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                OutlinedTextField(
                    value = name,
                    onValueChange = { name = it },
                    label = { Text("Label") },
                    singleLine = true,
                    modifier = Modifier.fillMaxWidth()
                )
                OutlinedTextField(
                    value = blob,
                    onValueChange = { blob = it },
                    label = { Text("Backup blob (paste here)") },
                    minLines = 3,
                    modifier = Modifier.fillMaxWidth()
                )
                OutlinedTextField(
                    value = passphrase,
                    onValueChange = { passphrase = it },
                    label = { Text("Passphrase") },
                    visualTransformation = PasswordVisualTransformation(),
                    singleLine = true,
                    modifier = Modifier.fillMaxWidth()
                )
            }
        },
        confirmButton = {
            TextButton(
                onClick = { onConfirm(name, blob.trim(), passphrase.toCharArray()) },
                enabled = blob.isNotBlank() && passphrase.isNotEmpty()
            ) { Text("Import") }
        },
        dismissButton = { TextButton(onClick = onDismiss) { Text("Cancel") } }
    )
}
