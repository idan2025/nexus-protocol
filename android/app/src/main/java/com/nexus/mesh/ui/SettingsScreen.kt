package com.nexus.mesh.ui

import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import androidx.navigation.NavController

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
    val networkState by service?.networkState?.collectAsState()
        ?: remember { mutableStateOf(com.nexus.mesh.service.NetworkState()) }

    val tcpConfig = service?.getTcpConfig()
    var tcpPort by remember { mutableStateOf(tcpConfig?.first?.toString() ?: "4242") }
    var tcpPeers by remember { mutableStateOf(tcpConfig?.second ?: "") }
    var editName by remember(myName) { mutableStateOf(myName) }
    var editPillars by remember(pillarList) { mutableStateOf(pillarList) }
    var showAddPillar by remember { mutableStateOf(false) }
    var newPillar by remember { mutableStateOf("") }

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
