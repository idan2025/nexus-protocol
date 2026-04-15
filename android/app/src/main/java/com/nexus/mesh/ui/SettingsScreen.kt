package com.nexus.mesh.ui

import android.content.Intent
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.input.PasswordVisualTransformation
import androidx.compose.ui.unit.dp
import androidx.navigation.NavController
import com.nexus.mesh.data.IdentityBackup

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun SettingsScreen(activity: MainActivity, navController: NavController? = null) {
    val service = activity.getService()
    val address by service?.address?.collectAsState() ?: remember { mutableStateOf("--------") }
    val tcpActive by service?.tcpActive?.collectAsState() ?: remember { mutableStateOf(false) }
    val udpActive by service?.udpActive?.collectAsState() ?: remember { mutableStateOf(false) }
    val myName by service?.myName?.collectAsState() ?: remember { mutableStateOf("") }
    val pillarsEnabled by service?.pillarsEnabled?.collectAsState() ?: remember { mutableStateOf(true) }
    val pillarList by service?.pillarList?.collectAsState() ?: remember { mutableStateOf("") }
    val pillarConnected by service?.pillarConnected?.collectAsState() ?: remember { mutableStateOf(false) }
    val pillarsAllowMetered by service?.pillarsAllowMetered?.collectAsState() ?: remember { mutableStateOf(false) }
    val networkState by service?.networkState?.collectAsState()
        ?: remember { mutableStateOf(com.nexus.mesh.service.NetworkState()) }

    val tcpConfig = service?.getTcpConfig()
    var tcpPort by remember { mutableStateOf(tcpConfig?.first?.toString() ?: "4242") }
    var tcpPeers by remember { mutableStateOf(tcpConfig?.second ?: "") }
    var editName by remember(myName) { mutableStateOf(myName) }
    var editPillars by remember(pillarList) { mutableStateOf(pillarList) }
    var showAddPillar by remember { mutableStateOf(false) }
    var newPillar by remember { mutableStateOf("") }
    var showExportDialog by remember { mutableStateOf(false) }
    var showImportDialog by remember { mutableStateOf(false) }

    val context = LocalContext.current

    Scaffold(
        topBar = {
            TopAppBar(title = { Text("Settings") })
        }
    ) { padding ->
        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(padding)
                .verticalScroll(rememberScrollState())
                .padding(16.dp),
            verticalArrangement = Arrangement.spacedBy(16.dp)
        ) {
            // Identity
            Card(modifier = Modifier.fillMaxWidth()) {
                Column(modifier = Modifier.padding(16.dp)) {
                    Text("Identity", style = MaterialTheme.typography.titleMedium)
                    Spacer(Modifier.height(8.dp))
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.SpaceBetween
                    ) {
                        Text("Short Address")
                        Text(address, color = MaterialTheme.colorScheme.primary)
                    }
                    Spacer(Modifier.height(12.dp))
                    OutlinedTextField(
                        value = editName,
                        onValueChange = { editName = it },
                        label = { Text("Your Name") },
                        placeholder = { Text("e.g. Alice, Bob") },
                        singleLine = true,
                        modifier = Modifier.fillMaxWidth()
                    )
                    Spacer(Modifier.height(8.dp))
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.End
                    ) {
                        if (myName.isNotEmpty()) {
                            TextButton(onClick = {
                                editName = ""
                                service?.setMyName("")
                            }) {
                                Text("Clear")
                            }
                            Spacer(Modifier.width(8.dp))
                        }
                        Button(
                            onClick = { service?.setMyName(editName) },
                            enabled = editName != myName
                        ) {
                            Text("Save Name")
                        }
                    }
                    if (navController != null) {
                        Spacer(Modifier.height(8.dp))
                        OutlinedButton(
                            onClick = { navController.navigate("qr") },
                            modifier = Modifier.fillMaxWidth()
                        ) {
                            Text("QR Code")
                        }
                    }

                    Spacer(Modifier.height(12.dp))
                    Divider()
                    Spacer(Modifier.height(12.dp))
                    Text("Backup", style = MaterialTheme.typography.titleSmall)
                    Spacer(Modifier.height(4.dp))
                    Text(
                        "Encrypted export of your keys. Required to keep the same " +
                            "address on a new device.",
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                    Spacer(Modifier.height(8.dp))
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.spacedBy(8.dp)
                    ) {
                        OutlinedButton(
                            onClick = { showExportDialog = true },
                            modifier = Modifier.weight(1f),
                            enabled = service != null
                        ) { Text("Export") }
                        OutlinedButton(
                            onClick = { showImportDialog = true },
                            modifier = Modifier.weight(1f),
                            enabled = service != null
                        ) { Text("Import") }
                    }
                }
            }

            // Network Interfaces (Dynamic Detection)
            Card(modifier = Modifier.fillMaxWidth()) {
                Column(modifier = Modifier.padding(16.dp)) {
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.SpaceBetween,
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        Text("Network Interfaces", style = MaterialTheme.typography.titleMedium)
                        StatusBadge(active = networkState.hasAnyInternet)
                    }
                    Spacer(Modifier.height(4.dp))
                    Text(
                        "Detected interfaces are used automatically. " +
                        "UDP multicast runs only on WiFi/Ethernet.",
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                    Spacer(Modifier.height(12.dp))

                    if (networkState.interfaces.isEmpty()) {
                        Text(
                            "No network interfaces detected",
                            style = MaterialTheme.typography.bodyMedium,
                            color = MaterialTheme.colorScheme.error
                        )
                    } else {
                        networkState.interfaces.forEach { iface ->
                            Row(
                                modifier = Modifier
                                    .fillMaxWidth()
                                    .padding(vertical = 4.dp),
                                horizontalArrangement = Arrangement.SpaceBetween,
                                verticalAlignment = Alignment.CenterVertically
                            ) {
                                Column {
                                    Text(
                                        iface.type + (iface.name?.let { " ($it)" } ?: ""),
                                        style = MaterialTheme.typography.bodyMedium
                                    )
                                    if (iface.addresses.isNotEmpty()) {
                                        Text(
                                            iface.addresses.joinToString(", "),
                                            style = MaterialTheme.typography.bodySmall,
                                            color = MaterialTheme.colorScheme.onSurfaceVariant
                                        )
                                    }
                                }
                                if (iface.isMetered) {
                                    Surface(
                                        shape = MaterialTheme.shapes.small,
                                        color = MaterialTheme.colorScheme.tertiaryContainer
                                    ) {
                                        Text(
                                            "Metered",
                                            modifier = Modifier.padding(
                                                horizontal = 6.dp, vertical = 2.dp
                                            ),
                                            style = MaterialTheme.typography.labelSmall,
                                            color = MaterialTheme.colorScheme.onTertiaryContainer
                                        )
                                    }
                                }
                            }
                        }
                    }

                    if (!networkState.canMulticast && networkState.hasAnyInternet) {
                        Spacer(Modifier.height(8.dp))
                        Text(
                            "LAN discovery paused (no WiFi/Ethernet)",
                            style = MaterialTheme.typography.bodySmall,
                            color = MaterialTheme.colorScheme.tertiary
                        )
                    }
                }
            }

            // Pillar Nodes (Internet Connectivity)
            Card(modifier = Modifier.fillMaxWidth()) {
                Column(modifier = Modifier.padding(16.dp)) {
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.SpaceBetween,
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        Text("Pillar Nodes", style = MaterialTheme.typography.titleMedium)
                        StatusBadge(active = pillarConnected)
                    }
                    Spacer(Modifier.height(4.dp))
                    Text(
                        "Pillars are public NEXUS relay nodes on the internet. " +
                        "Connect outbound to them for global mesh access -- no port forwarding needed.",
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                    Spacer(Modifier.height(12.dp))

                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.SpaceBetween,
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        Text("Auto-connect to Pillars")
                        Switch(
                            checked = pillarsEnabled,
                            onCheckedChange = { enabled ->
                                service?.setPillars(enabled, editPillars)
                            }
                        )
                    }
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.SpaceBetween,
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        Column(modifier = Modifier.weight(1f)) {
                            Text("Use Pillars on metered networks")
                            Text(
                                "When off, NEXUS waits for WiFi/Ethernet before " +
                                    "talking to Pillars over the Internet.",
                                style = MaterialTheme.typography.bodySmall,
                                color = MaterialTheme.colorScheme.onSurfaceVariant
                            )
                        }
                        Switch(
                            checked = pillarsAllowMetered,
                            onCheckedChange = { service?.setPillarsAllowMetered(it) }
                        )
                    }

                    Spacer(Modifier.height(8.dp))
                    OutlinedTextField(
                        value = editPillars,
                        onValueChange = { editPillars = it },
                        label = { Text("Pillar addresses (host:port, comma separated)") },
                        placeholder = { Text("e.g. pillar.example.com:4242, 1.2.3.4:4242") },
                        modifier = Modifier.fillMaxWidth(),
                        singleLine = false,
                        maxLines = 4
                    )
                    Spacer(Modifier.height(8.dp))
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.End
                    ) {
                        Button(
                            onClick = { service?.setPillars(pillarsEnabled, editPillars) },
                            enabled = editPillars != pillarList
                        ) {
                            Text("Save Pillars")
                        }
                    }
                }
            }

            // TCP Internet Transport (Advanced)
            Card(modifier = Modifier.fillMaxWidth()) {
                Column(modifier = Modifier.padding(16.dp)) {
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.SpaceBetween
                    ) {
                        Text("TCP (Advanced)", style = MaterialTheme.typography.titleMedium)
                        StatusBadge(active = tcpActive)
                    }
                    Spacer(Modifier.height(4.dp))
                    Text(
                        "Manual TCP configuration. Use this to run your own Pillar node or connect directly to specific peers.",
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                    Spacer(Modifier.height(12.dp))

                    OutlinedTextField(
                        value = tcpPort,
                        onValueChange = { tcpPort = it.filter { c -> c.isDigit() } },
                        label = { Text("Listen Port (0 = client only)") },
                        modifier = Modifier.fillMaxWidth(),
                        singleLine = true
                    )
                    Spacer(Modifier.height(8.dp))

                    OutlinedTextField(
                        value = tcpPeers,
                        onValueChange = { tcpPeers = it },
                        label = { Text("Peers (host:port, comma separated)") },
                        modifier = Modifier.fillMaxWidth(),
                        singleLine = false,
                        maxLines = 3
                    )
                    Spacer(Modifier.height(12.dp))

                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.spacedBy(8.dp)
                    ) {
                        Button(
                            onClick = {
                                val port = tcpPort.toIntOrNull() ?: 4242
                                service?.startTcpInet(port, tcpPeers)
                            },
                            enabled = !tcpActive,
                            modifier = Modifier.weight(1f)
                        ) {
                            Text("Connect")
                        }
                        OutlinedButton(
                            onClick = { service?.stopTcpInet() },
                            enabled = tcpActive,
                            modifier = Modifier.weight(1f)
                        ) {
                            Text("Disconnect")
                        }
                    }
                }
            }

            // UDP Multicast
            Card(modifier = Modifier.fillMaxWidth()) {
                Column(modifier = Modifier.padding(16.dp)) {
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.SpaceBetween
                    ) {
                        Text(
                            "LAN Discovery",
                            style = MaterialTheme.typography.titleMedium
                        )
                        StatusBadge(active = udpActive)
                    }
                    Spacer(Modifier.height(8.dp))
                    Text(
                        "Auto-discovers NEXUS nodes on local network via UDP multicast.",
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                    Spacer(Modifier.height(12.dp))
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.spacedBy(8.dp)
                    ) {
                        Button(
                            onClick = { service?.startUdpMulticast(userTriggered = true) },
                            enabled = !udpActive,
                            modifier = Modifier.weight(1f)
                        ) {
                            Text("Enable")
                        }
                        OutlinedButton(
                            onClick = { service?.stopUdpMulticast(userTriggered = true) },
                            enabled = udpActive,
                            modifier = Modifier.weight(1f)
                        ) {
                            Text("Disable")
                        }
                    }
                }
            }

            // About
            Card(modifier = Modifier.fillMaxWidth()) {
                Column(modifier = Modifier.padding(16.dp)) {
                    Text("About", style = MaterialTheme.typography.titleMedium)
                    Spacer(Modifier.height(8.dp))
                    Text("NEXUS Mesh v0.1.0")
                    Spacer(Modifier.height(4.dp))
                    Text(
                        "E2E encrypted multi-transport mesh protocol",
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                    Spacer(Modifier.height(4.dp))
                    Text(
                        "Crypto: XChaCha20-Poly1305, Ed25519, X25519",
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                    Text(
                        "Transports: LoRa, BLE, TCP, UDP Multicast, WiFi HaLow",
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                    Spacer(Modifier.height(8.dp))
                    Text(
                        "Node Roles",
                        style = MaterialTheme.typography.labelMedium
                    )
                    Text(
                        "Leaf: endpoint only\n" +
                        "Relay: forwards + stores 8 msgs (30min)\n" +
                        "Anchor: stores 32 msgs (1hr)\n" +
                        "Pillar: public internet relay\n" +
                        "Vault: stores 256 msgs (24hr)",
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                }
            }

            Spacer(Modifier.height(16.dp))
        }
    }

    if (showExportDialog) {
        ExportIdentityDialog(
            onDismiss = { showExportDialog = false },
            onExport = { passphrase ->
                val blob = service?.exportIdentity(passphrase.toCharArray())
                if (blob != null) {
                    val intent = Intent(Intent.ACTION_SEND).apply {
                        type = "text/plain"
                        putExtra(Intent.EXTRA_SUBJECT, "NEXUS Identity Backup")
                        putExtra(Intent.EXTRA_TEXT, blob)
                    }
                    context.startActivity(Intent.createChooser(intent, "Share identity backup"))
                }
                showExportDialog = false
            }
        )
    }

    if (showImportDialog) {
        ImportIdentityDialog(
            onDismiss = { showImportDialog = false },
            onImport = { blob, passphrase ->
                try {
                    val ok = service?.importIdentity(blob, passphrase.toCharArray()) ?: false
                    if (ok) showImportDialog = false
                    ok to null
                } catch (e: IdentityBackup.BadPassphraseException) {
                    false to "Wrong passphrase"
                } catch (e: IdentityBackup.BadBackupException) {
                    false to (e.message ?: "Invalid backup")
                } catch (e: Exception) {
                    false to (e.message ?: "Import failed")
                }
            }
        )
    }
}

@Composable
private fun ExportIdentityDialog(
    onDismiss: () -> Unit,
    onExport: (String) -> Unit,
) {
    var passphrase by remember { mutableStateOf("") }
    var confirm by remember { mutableStateOf("") }
    val valid = passphrase.length >= 8 && passphrase == confirm

    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text("Export Identity") },
        text = {
            Column {
                Text(
                    "Choose a passphrase (8+ characters). You'll need it to restore " +
                        "on another device. Losing it means losing the backup.",
                    style = MaterialTheme.typography.bodySmall
                )
                Spacer(Modifier.height(12.dp))
                OutlinedTextField(
                    value = passphrase,
                    onValueChange = { passphrase = it },
                    label = { Text("Passphrase") },
                    visualTransformation = PasswordVisualTransformation(),
                    singleLine = true,
                    modifier = Modifier.fillMaxWidth()
                )
                Spacer(Modifier.height(8.dp))
                OutlinedTextField(
                    value = confirm,
                    onValueChange = { confirm = it },
                    label = { Text("Confirm passphrase") },
                    visualTransformation = PasswordVisualTransformation(),
                    singleLine = true,
                    isError = confirm.isNotEmpty() && confirm != passphrase,
                    modifier = Modifier.fillMaxWidth()
                )
            }
        },
        confirmButton = {
            TextButton(
                onClick = { onExport(passphrase) },
                enabled = valid
            ) { Text("Export") }
        },
        dismissButton = {
            TextButton(onClick = onDismiss) { Text("Cancel") }
        }
    )
}

@Composable
private fun ImportIdentityDialog(
    onDismiss: () -> Unit,
    onImport: (blob: String, passphrase: String) -> Pair<Boolean, String?>,
) {
    var blob by remember { mutableStateOf("") }
    var passphrase by remember { mutableStateOf("") }
    var confirmReplace by remember { mutableStateOf(false) }
    var error by remember { mutableStateOf<String?>(null) }

    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text("Import Identity") },
        text = {
            Column {
                Text(
                    "This replaces your current identity. Your address will change " +
                        "to match the backup. Existing conversations remain but peers " +
                        "may need to re-announce.",
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.error
                )
                Spacer(Modifier.height(12.dp))
                OutlinedTextField(
                    value = blob,
                    onValueChange = { blob = it; error = null },
                    label = { Text("Backup blob") },
                    placeholder = { Text("Paste the exported text") },
                    maxLines = 4,
                    modifier = Modifier.fillMaxWidth()
                )
                Spacer(Modifier.height(8.dp))
                OutlinedTextField(
                    value = passphrase,
                    onValueChange = { passphrase = it; error = null },
                    label = { Text("Passphrase") },
                    visualTransformation = PasswordVisualTransformation(),
                    singleLine = true,
                    modifier = Modifier.fillMaxWidth()
                )
                Spacer(Modifier.height(8.dp))
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Checkbox(
                        checked = confirmReplace,
                        onCheckedChange = { confirmReplace = it }
                    )
                    Text("Replace current identity", style = MaterialTheme.typography.bodySmall)
                }
                if (error != null) {
                    Spacer(Modifier.height(8.dp))
                    Text(
                        error!!,
                        color = MaterialTheme.colorScheme.error,
                        style = MaterialTheme.typography.bodySmall
                    )
                }
            }
        },
        confirmButton = {
            TextButton(
                onClick = {
                    val (ok, err) = onImport(blob.trim(), passphrase)
                    if (!ok) error = err ?: "Import failed"
                },
                enabled = blob.isNotBlank() && passphrase.isNotEmpty() && confirmReplace
            ) { Text("Import") }
        },
        dismissButton = {
            TextButton(onClick = onDismiss) { Text("Cancel") }
        }
    )
}

@Composable
private fun StatusBadge(active: Boolean) {
    Surface(
        shape = MaterialTheme.shapes.small,
        color = if (active) MaterialTheme.colorScheme.primaryContainer
                else MaterialTheme.colorScheme.surfaceVariant
    ) {
        Text(
            if (active) "Active" else "Inactive",
            modifier = Modifier.padding(horizontal = 8.dp, vertical = 4.dp),
            style = MaterialTheme.typography.labelSmall,
            color = if (active) MaterialTheme.colorScheme.onPrimaryContainer
                    else MaterialTheme.colorScheme.onSurfaceVariant
        )
    }
}
