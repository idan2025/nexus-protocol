package com.nexus.mesh.ui

import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.ArrowBack
import androidx.compose.material.icons.filled.Delete
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import androidx.navigation.NavController
import com.nexus.mesh.data.ContactEntity
import kotlinx.coroutines.launch
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun ContactsScreen(activity: MainActivity, navController: NavController) {
    val service = activity.getService()
    val contacts by service?.repository?.getContacts()?.collectAsState(initial = emptyList())
        ?: remember { mutableStateOf(emptyList()) }
    val scope = rememberCoroutineScope()

    var editing by remember { mutableStateOf<ContactEntity?>(null) }
    var showAdd by remember { mutableStateOf(false) }

    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text("Contacts") },
                navigationIcon = {
                    IconButton(onClick = { navController.popBackStack() }) {
                        Icon(Icons.Default.ArrowBack, "Back")
                    }
                }
            )
        },
        floatingActionButton = {
            ExtendedFloatingActionButton(onClick = { showAdd = true }) {
                Text("Add")
            }
        }
    ) { padding ->
        if (contacts.isEmpty()) {
            Box(
                Modifier.fillMaxSize().padding(padding),
                contentAlignment = Alignment.Center
            ) {
                Text(
                    "No contacts yet.\nOpen 'Announces' to add nodes you see.",
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
                items(contacts, key = { it.address }) { c ->
                    ContactCard(
                        contact = c,
                        onEdit = { editing = c },
                        onOpen = { navController.navigate("conversation/${c.address}") },
                        onDelete = {
                            scope.launch { service?.repository?.deleteContact(c) }
                        }
                    )
                }
            }
        }
    }

    editing?.let { contact ->
        NicknameEditDialog(
            initial = contact.nickname ?: "",
            onDismiss = { editing = null },
            onSave = { name ->
                scope.launch {
                    service?.repository?.upsertContact(
                        contact.copy(nickname = name.ifBlank { null })
                    )
                }
                editing = null
            }
        )
    }

    if (showAdd) {
        AddContactDialog(
            onDismiss = { showAdd = false },
            onAdd = { addr, nick ->
                val normalized = addr.trim().uppercase()
                if (normalized.length == 8 && normalized.all { it.isDigit() || it in 'A'..'F' }) {
                    scope.launch {
                        service?.repository?.upsertContact(
                            ContactEntity(address = normalized,
                                          nickname = nick.ifBlank { null })
                        )
                    }
                }
                showAdd = false
            }
        )
    }
}

@Composable
private fun ContactCard(
    contact: ContactEntity,
    onEdit: () -> Unit,
    onOpen: () -> Unit,
    onDelete: () -> Unit
) {
    val dfmt = remember { SimpleDateFormat("yyyy-MM-dd", Locale.getDefault()) }
    Card(modifier = Modifier.fillMaxWidth().clickable(onClick = onOpen)) {
        Column(Modifier.padding(12.dp)) {
            Row(
                Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically
            ) {
                Column(Modifier.weight(1f)) {
                    Text(
                        contact.nickname ?: contact.address,
                        style = MaterialTheme.typography.titleMedium
                    )
                    if (contact.nickname != null) {
                        Text(
                            contact.address,
                            style = MaterialTheme.typography.bodySmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                    }
                }
                IconButton(onClick = onDelete) {
                    Icon(Icons.Default.Delete, "Delete")
                }
            }
            Spacer(Modifier.height(4.dp))
            Text(
                "seen ${dfmt.format(Date(contact.lastSeen))}",
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
            Spacer(Modifier.height(4.dp))
            TextButton(onClick = onEdit) { Text("Set nickname") }
        }
    }
}

@Composable
private fun NicknameEditDialog(
    initial: String,
    onDismiss: () -> Unit,
    onSave: (String) -> Unit
) {
    var name by remember { mutableStateOf(initial) }
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text("Set nickname") },
        text = {
            OutlinedTextField(
                value = name,
                onValueChange = { name = it.take(32) },
                singleLine = true,
                label = { Text("Nickname") }
            )
        },
        confirmButton = { TextButton(onClick = { onSave(name) }) { Text("Save") } },
        dismissButton = { TextButton(onClick = onDismiss) { Text("Cancel") } }
    )
}

@Composable
private fun AddContactDialog(
    onDismiss: () -> Unit,
    onAdd: (String, String) -> Unit
) {
    var addr by remember { mutableStateOf("") }
    var nick by remember { mutableStateOf("") }
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text("Add contact") },
        text = {
            Column {
                OutlinedTextField(
                    value = addr,
                    onValueChange = { addr = it.take(8) },
                    singleLine = true,
                    label = { Text("Address (8 hex chars)") }
                )
                Spacer(Modifier.height(8.dp))
                OutlinedTextField(
                    value = nick,
                    onValueChange = { nick = it.take(32) },
                    singleLine = true,
                    label = { Text("Nickname (optional)") }
                )
            }
        },
        confirmButton = { TextButton(onClick = { onAdd(addr, nick) }) { Text("Add") } },
        dismissButton = { TextButton(onClick = onDismiss) { Text("Cancel") } }
    )
}
