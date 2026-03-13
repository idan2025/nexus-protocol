package com.nexus.mesh.ui

import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.filled.Add
import androidx.compose.material.icons.filled.Person
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import androidx.navigation.NavController

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun GroupInfoScreen(
    activity: MainActivity,
    navController: NavController,
    groupId: String
) {
    val service = activity.getService()
    var showAddMember by remember { mutableStateOf(false) }
    var newMemberAddr by remember { mutableStateOf("") }

    // Get group members via JNI
    val groupIdBytes = remember(groupId) {
        try {
            ByteArray(4) { groupId.substring(it * 2, it * 2 + 2).toInt(16).toByte() }
        } catch (e: Exception) { null }
    }

    var members by remember { mutableStateOf<List<String>>(emptyList()) }

    LaunchedEffect(groupId) {
        if (groupIdBytes != null) {
            try {
                val field = service?.javaClass?.getDeclaredField("node")
                field?.isAccessible = true
                val node = field?.get(service) as? com.nexus.mesh.service.NexusNode
                val memberArrays = node?.groupGetMembers(groupIdBytes)
                members = memberArrays?.map { bytes ->
                    bytes.joinToString("") { "%02X".format(it) }
                } ?: emptyList()
            } catch (e: Exception) {
                members = emptyList()
            }
        }
    }

    Scaffold(
        topBar = {
            TopAppBar(
                title = {
                    Column {
                        Text("Group Info")
                        Text(
                            groupId,
                            style = MaterialTheme.typography.bodySmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                    }
                },
                navigationIcon = {
                    IconButton(onClick = { navController.popBackStack() }) {
                        Icon(Icons.AutoMirrored.Filled.ArrowBack, "Back")
                    }
                }
            )
        },
        floatingActionButton = {
            FloatingActionButton(onClick = { showAddMember = true }) {
                Icon(Icons.Default.Add, "Add Member")
            }
        }
    ) { padding ->
        LazyColumn(
            modifier = Modifier
                .fillMaxSize()
                .padding(padding),
            contentPadding = PaddingValues(16.dp),
            verticalArrangement = Arrangement.spacedBy(8.dp)
        ) {
            item {
                Card(modifier = Modifier.fillMaxWidth()) {
                    Column(modifier = Modifier.padding(16.dp)) {
                        Text("Group ID", style = MaterialTheme.typography.labelMedium)
                        Text(groupId, style = MaterialTheme.typography.bodyLarge,
                            color = MaterialTheme.colorScheme.primary)
                        Spacer(Modifier.height(8.dp))
                        Text("${members.size} members",
                            style = MaterialTheme.typography.bodyMedium,
                            color = MaterialTheme.colorScheme.onSurfaceVariant)
                    }
                }
            }

            item {
                Text(
                    "Members",
                    style = MaterialTheme.typography.titleMedium,
                    modifier = Modifier.padding(vertical = 8.dp)
                )
            }

            items(members) { memberAddr ->
                Card(modifier = Modifier.fillMaxWidth()) {
                    Row(
                        modifier = Modifier
                            .fillMaxWidth()
                            .padding(16.dp),
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        Icon(
                            Icons.Default.Person,
                            contentDescription = null,
                            tint = MaterialTheme.colorScheme.primary
                        )
                        Spacer(Modifier.width(12.dp))
                        Text(
                            memberAddr,
                            style = MaterialTheme.typography.bodyMedium
                        )
                    }
                }
            }
        }
    }

    // Add member dialog
    if (showAddMember) {
        AlertDialog(
            onDismissRequest = { showAddMember = false; newMemberAddr = "" },
            title = { Text("Add Member") },
            text = {
                OutlinedTextField(
                    value = newMemberAddr,
                    onValueChange = { newMemberAddr = it.uppercase().filter { c -> c in "0123456789ABCDEF" }.take(8) },
                    label = { Text("Member Address (8 hex chars)") },
                    singleLine = true,
                    modifier = Modifier.fillMaxWidth()
                )
            },
            confirmButton = {
                Button(
                    onClick = {
                        if (newMemberAddr.length == 8 && groupIdBytes != null) {
                            try {
                                val memberBytes = ByteArray(4) {
                                    newMemberAddr.substring(it * 2, it * 2 + 2).toInt(16).toByte()
                                }
                                val field = service?.javaClass?.getDeclaredField("node")
                                field?.isAccessible = true
                                val node = field?.get(service) as? com.nexus.mesh.service.NexusNode
                                node?.groupAddMember(groupIdBytes, memberBytes)
                                // Refresh member list
                                val memberArrays = node?.groupGetMembers(groupIdBytes)
                                members = memberArrays?.map { bytes ->
                                    bytes.joinToString("") { "%02X".format(it) }
                                } ?: emptyList()
                            } catch (e: Exception) { /* ignore */ }
                            showAddMember = false
                            newMemberAddr = ""
                        }
                    },
                    enabled = newMemberAddr.length == 8
                ) {
                    Text("Add")
                }
            },
            dismissButton = {
                TextButton(onClick = { showAddMember = false; newMemberAddr = "" }) {
                    Text("Cancel")
                }
            }
        )
    }
}
