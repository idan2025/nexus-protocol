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
import com.nexus.mesh.BuildConfig
import com.nexus.mesh.data.IdentityBackup
import com.nexus.mesh.updater.UpdateSettingsRow
import com.nexus.mesh.updater.UpdateState

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun SettingsScreen(
    activity: MainActivity,
    navController: NavController? = null,
    updateState: UpdateState? = null,
) {
    val service = activity.getService()
    val address by service?.address?.collectAsState() ?: remember { mutableStateOf("--------") }
    val tcpActive by service?.tcpActive?.collectAsState() ?: remember { mutableStateOf(false) }
    val udpActive by service?.udpActive?.collectAsState() ?: remember { mutableStateOf(false) }
    val myName by service?.myName?.collectAsState() ?: remember { mutableStateOf("") }
    val pillarsEnabled by service?.pillarsEnabled?.collectAsState() ?: remember { mutableStateOf(true) }
    val pillarList by service?.pillarList?.collectAsState() ?: remember { mutableStateOf("") }
    val pillarConnected by service?.pillarConnected?.collectAsState() ?: remember { mutableStateOf(false) }
    val pillarsAllowMetered by service?.pillarsAllowMetered?.collectAsState() ?: remember { mutableStateOf(false) }
    val socksProxyEnabled by service?.socksProxyEnabled?.collectAsState() ?: remember { mutableStateOf(false) }
    val socksProxyHost by service?.socksProxyHost?.collectAsState() ?: remember { mutableStateOf(com.nexus.mesh.service.NexusService.DEFAULT_SOCKS5_HOST) }
    val socksProxyPort by service?.socksProxyPort?.collectAsState() ?: remember { mutableStateOf(com.nexus.mesh.service.NexusService.DEFAULT_SOCKS5_PORT) }
    val discoveredPillars by service?.discoveredPillars?.collectAsState() ?: remember { mutableStateOf(emptyList<String>()) }
    val networkState by service?.networkState?.collectAsState()
        ?: remember { mutableStateOf(com.nexus.mesh.service.NetworkState()) }
    val stampStats by service?.stampStats?.collectAsState()
        ?: remember { mutableStateOf(com.nexus.mesh.service.NexusService.StampStats()) }
    val stampMinDifficulty by service?.stampMinDifficulty?.collectAsState() ?: remember { mutableStateOf(0) }
    val stampReject by service?.stampReject?.collectAsState() ?: remember { mutableStateOf(false) }
    val announceIntervalMs by service?.announceIntervalMs?.collectAsState()
        ?: remember { mutableStateOf(com.nexus.mesh.service.NexusService.DEFAULT_ANNOUNCE_INTERVAL_MS) }
    var showAnnouncePicker by remember { mutableStateOf(false) }
    val currentRole by service?.role?.collectAsState()
        ?: remember { mutableStateOf(com.nexus.mesh.service.NexusService.DEFAULT_ROLE) }
    var showRolePicker by remember { mutableStateOf(false) }

    val tcpConfig = service?.getTcpConfig()
    var tcpPort by remember { mutableStateOf(tcpConfig?.first?.toString() ?: "4242") }
    var tcpPeers by remember { mutableStateOf(tcpConfig?.second ?: "") }
    var editName by remember(myName) { mutableStateOf(myName) }
    var editPillars by remember(pillarList) { mutableStateOf(pillarList) }
    var showAddPillar by remember { mutableStateOf(false) }
    var newPillar by remember { mutableStateOf("") }
    var editSocksHost by remember(socksProxyHost) { mutableStateOf(socksProxyHost) }
    var editSocksPort by remember(socksProxyPort) { mutableStateOf(socksProxyPort.toString()) }
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
            // App update + flasher hub (rendered first so they're easy
            // to reach; the rest of the screen is unchanged).
            if (updateState != null) {
                UpdateSettingsRow(state = updateState, activity = activity)
            }
            if (navController != null) {
                Card(modifier = Modifier.fillMaxWidth()) {
                    Column(modifier = Modifier.padding(16.dp)) {
                        Text("Flash Nodes", style = MaterialTheme.typography.titleMedium)
                        Spacer(Modifier.height(4.dp))
                        Text(
                            "Download firmware for any supported board, " +
                                "flash an ESP32 over USB-OTG, or push BLE-DFU " +
                                "OTA to a connected nRF52 node.",
                            style = MaterialTheme.typography.bodySmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                        Spacer(Modifier.height(8.dp))
                        Button(
                            onClick = { navController.navigate("flash_node") },
                            modifier = Modifier.fillMaxWidth()
                        ) { Text("Open Flash Node") }
                    }
                }
                Card(modifier = Modifier.fillMaxWidth()) {
                    Column(modifier = Modifier.padding(16.dp)) {
                        Text("Identities", style = MaterialTheme.typography.titleMedium)
                        Spacer(Modifier.height(4.dp))
                        Text(
                            "Create, import, switch, or delete NEXUS identities. " +
                                "Each identity has its own mesh address and conversation history.",
                            style = MaterialTheme.typography.bodySmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                        Spacer(Modifier.height(8.dp))
                        Button(
                            onClick = { navController.navigate("identities") },
                            modifier = Modifier.fillMaxWidth()
                        ) { Text("Manage Identities") }
                    }
                }
                Card(modifier = Modifier.fillMaxWidth()) {
                    Column(modifier = Modifier.padding(16.dp)) {
                        Text("Offline Maps", style = MaterialTheme.typography.titleMedium)
                        Spacer(Modifier.height(4.dp))
                        Text(
                            "Download or import MBTiles archives for offline map viewing. " +
                                "Supports both vector and raster tile formats.",
                            style = MaterialTheme.typography.bodySmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                        Spacer(Modifier.height(8.dp))
                        Button(
                            onClick = { navController.navigate("offline_maps") },
                            modifier = Modifier.fillMaxWidth()
                        ) { Text("Manage Offline Maps") }
                    }
                }
            }

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

            // Announce -- like Reticulum's announce-now button plus a
            // configurable cadence. "Off" disables the auto loop but the
            // manual button always works.
            Card(modifier = Modifier.fillMaxWidth()) {
                Column(modifier = Modifier.padding(16.dp)) {
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.SpaceBetween,
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        Text("Announce", style = MaterialTheme.typography.titleMedium)
                        Button(
                            onClick = { service?.forceAnnounce() },
                            enabled = service != null
                        ) { Text("Announce now") }
                    }
                    Spacer(Modifier.height(4.dp))
                    Text(
                        "Broadcast this node's identity so neighbors can route to it. " +
                        "Reticulum-style: tap to send one now, or set an automatic cadence.",
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                    Spacer(Modifier.height(4.dp))
                    SettingsRow(
                        label = "Auto-announce every",
                        value = formatAnnounceInterval(announceIntervalMs)
                    ) { showAnnouncePicker = true }
                }
            }

            // Node Role -- LEAF/RELAY/GATEWAY. Heavier roles (Anchor, Vault,
            // Pillar) are designed for always-on infra and aren't exposed
            // here; pick them in the daemon config instead.
            Card(modifier = Modifier.fillMaxWidth()) {
                Column(modifier = Modifier.padding(16.dp)) {
                    Text("Node Role", style = MaterialTheme.typography.titleMedium)
                    Spacer(Modifier.height(4.dp))
                    Text(
                        "How this device participates in the mesh. Change takes " +
                        "effect on next app start (Stop Mesh & Quit, then reopen).",
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                    Spacer(Modifier.height(8.dp))
                    SettingsRow(
                        label = "Role",
                        value = roleLabel(currentRole)
                    ) { showRolePicker = true }
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

            // DNS-SD discovered pillars (informational)
            if (discoveredPillars.isNotEmpty()) {
                Card(modifier = Modifier.fillMaxWidth()) {
                    Column(modifier = Modifier.padding(16.dp)) {
                        Text("LAN Pillars (auto-discovered)", style = MaterialTheme.typography.titleMedium)
                        Spacer(Modifier.height(4.dp))
                        Text(
                            "Found via mDNS on your local network. These are added automatically.",
                            style = MaterialTheme.typography.bodySmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                        Spacer(Modifier.height(8.dp))
                        discoveredPillars.forEach { pillar ->
                            Text(
                                pillar,
                                style = MaterialTheme.typography.bodyMedium,
                                modifier = Modifier.padding(vertical = 2.dp)
                            )
                        }
                    }
                }
            }

            // Tor / Orbot SOCKS5 Proxy
            Card(modifier = Modifier.fillMaxWidth()) {
                Column(modifier = Modifier.padding(16.dp)) {
                    Text("Tor / Orbot Proxy", style = MaterialTheme.typography.titleMedium)
                    Spacer(Modifier.height(4.dp))
                    Text(
                        "Route Pillar connections through a SOCKS5 proxy (e.g. Orbot). " +
                        "Enable Orbot first, then toggle this on.",
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                    Spacer(Modifier.height(12.dp))
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.SpaceBetween,
                        verticalAlignment = androidx.compose.ui.Alignment.CenterVertically
                    ) {
                        Text("Enable SOCKS5 proxy")
                        Switch(
                            checked = socksProxyEnabled,
                            onCheckedChange = { enabled ->
                                service?.setSocksProxy(
                                    enabled,
                                    editSocksHost.ifBlank { com.nexus.mesh.service.NexusService.DEFAULT_SOCKS5_HOST },
                                    editSocksPort.toIntOrNull() ?: com.nexus.mesh.service.NexusService.DEFAULT_SOCKS5_PORT
                                )
                            }
                        )
                    }
                    if (socksProxyEnabled) {
                        Spacer(Modifier.height(8.dp))
                        OutlinedTextField(
                            value = editSocksHost,
                            onValueChange = { editSocksHost = it },
                            label = { Text("Proxy host") },
                            placeholder = { Text("127.0.0.1") },
                            singleLine = true,
                            modifier = Modifier.fillMaxWidth()
                        )
                        Spacer(Modifier.height(8.dp))
                        OutlinedTextField(
                            value = editSocksPort,
                            onValueChange = { editSocksPort = it.filter { c -> c.isDigit() } },
                            label = { Text("Proxy port") },
                            placeholder = { Text("9050") },
                            singleLine = true,
                            modifier = Modifier.fillMaxWidth()
                        )
                        Spacer(Modifier.height(8.dp))
                        Button(
                            onClick = {
                                service?.setSocksProxy(
                                    true,
                                    editSocksHost.ifBlank { com.nexus.mesh.service.NexusService.DEFAULT_SOCKS5_HOST },
                                    editSocksPort.toIntOrNull() ?: com.nexus.mesh.service.NexusService.DEFAULT_SOCKS5_PORT
                                )
                            },
                            enabled = editSocksHost != socksProxyHost ||
                                      editSocksPort != socksProxyPort.toString()
                        ) {
                            Text("Save Proxy")
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

            // Proof-of-Work Stamps (anti-spam)
            Card(modifier = Modifier.fillMaxWidth()) {
                Column(modifier = Modifier.padding(16.dp)) {
                    Text(
                        "Stamp Verification",
                        style = MaterialTheme.typography.titleMedium
                    )
                    Spacer(Modifier.height(4.dp))
                    Text(
                        "Require inbound messages to carry a proof-of-work stamp. " +
                        "Higher difficulty = more CPU for senders = less spam.",
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                    Spacer(Modifier.height(12.dp))

                    Text(
                        "Minimum difficulty: ${stampMinDifficulty} bits" +
                            if (stampMinDifficulty == 0) " (advisory only)" else "",
                        style = MaterialTheme.typography.bodyMedium
                    )
                    Slider(
                        value = stampMinDifficulty.toFloat(),
                        onValueChange = {
                            service?.setStampMinDifficulty(it.toInt())
                        },
                        valueRange = 0f..16f,
                        steps = 15,
                    )

                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.SpaceBetween,
                        verticalAlignment = Alignment.CenterVertically,
                    ) {
                        Column(modifier = Modifier.weight(1f)) {
                            Text("Reject under-difficulty", style = MaterialTheme.typography.bodyMedium)
                            Text(
                                if (stampReject) "Drop messages below the minimum"
                                else "Warn only — still deliver the message",
                                style = MaterialTheme.typography.bodySmall,
                                color = MaterialTheme.colorScheme.onSurfaceVariant,
                            )
                        }
                        Switch(
                            checked = stampReject,
                            onCheckedChange = { service?.setStampReject(it) },
                            enabled = stampMinDifficulty > 0,
                        )
                    }

                    Spacer(Modifier.height(12.dp))
                    Text("Inbound counters", style = MaterialTheme.typography.labelMedium)
                    Spacer(Modifier.height(4.dp))
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.SpaceBetween,
                    ) {
                        Text("Accepted: ${stampStats.accepted}",
                            style = MaterialTheme.typography.bodySmall)
                        Text("Warned: ${stampStats.warned}",
                            style = MaterialTheme.typography.bodySmall,
                            color = MaterialTheme.colorScheme.tertiary)
                        Text("Rejected: ${stampStats.rejected}",
                            style = MaterialTheme.typography.bodySmall,
                            color = MaterialTheme.colorScheme.error)
                    }
                    Spacer(Modifier.height(8.dp))
                    OutlinedButton(
                        onClick = { service?.resetStampStats() },
                        modifier = Modifier.fillMaxWidth(),
                    ) {
                        Text("Reset counters")
                    }
                }
            }

            // App Data
            Card(modifier = Modifier.fillMaxWidth()) {
                Column(modifier = Modifier.padding(16.dp)) {
                    Text("App Data", style = MaterialTheme.typography.titleMedium)
                    Spacer(Modifier.height(4.dp))
                    Text(
                        "Contacts are auto-pruned after 30 days of inactivity. " +
                            "You can also clear them manually.",
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                    Spacer(Modifier.height(8.dp))
                    var showClearContactsConfirm by remember { mutableStateOf(false) }
                    OutlinedButton(
                        onClick = { showClearContactsConfirm = true },
                        modifier = Modifier.fillMaxWidth(),
                        colors = ButtonDefaults.outlinedButtonColors(
                            contentColor = MaterialTheme.colorScheme.error
                        )
                    ) {
                        Text("Clear All Contacts")
                    }
                    if (showClearContactsConfirm) {
                        AlertDialog(
                            onDismissRequest = { showClearContactsConfirm = false },
                            title = { Text("Clear All Contacts?") },
                            text = {
                                Text(
                                    "All discovered neighbors will be removed from your contact list. " +
                                        "Your messages and conversations are not affected."
                                )
                            },
                            confirmButton = {
                                TextButton(onClick = {
                                    service?.clearAllContacts()
                                    showClearContactsConfirm = false
                                }) {
                                    Text("Clear", color = MaterialTheme.colorScheme.error)
                                }
                            },
                            dismissButton = {
                                TextButton(onClick = { showClearContactsConfirm = false }) {
                                    Text("Cancel")
                                }
                            }
                        )
                    }
                }
            }

            Spacer(Modifier.height(8.dp))

            // Stop Mesh Service & Quit
            Card(modifier = Modifier.fillMaxWidth()) {
                Column(modifier = Modifier.padding(16.dp)) {
                    Text("Quit", style = MaterialTheme.typography.titleMedium)
                    Spacer(Modifier.height(4.dp))
                    Text(
                        "Stops the mesh service and exits the app. The node " +
                            "will go offline until you reopen NEXUS.",
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                    Spacer(Modifier.height(8.dp))
                    var showQuitConfirm by remember { mutableStateOf(false) }
                    OutlinedButton(
                        onClick = { showQuitConfirm = true },
                        modifier = Modifier.fillMaxWidth(),
                        colors = ButtonDefaults.outlinedButtonColors(
                            contentColor = MaterialTheme.colorScheme.error
                        )
                    ) {
                        Text("Stop Mesh & Quit")
                    }
                    if (showQuitConfirm) {
                        AlertDialog(
                            onDismissRequest = { showQuitConfirm = false },
                            title = { Text("Stop Mesh & Quit?") },
                            text = {
                                Text(
                                    "Background message delivery will stop. " +
                                        "You won't receive new messages until you " +
                                        "open NEXUS again."
                                )
                            },
                            confirmButton = {
                                TextButton(onClick = {
                                    showQuitConfirm = false
                                    val intent = Intent(activity,
                                        com.nexus.mesh.service.NexusService::class.java)
                                    activity.stopService(intent)
                                    activity.finishAffinity()
                                }) {
                                    Text("Quit",
                                        color = MaterialTheme.colorScheme.error)
                                }
                            },
                            dismissButton = {
                                TextButton(onClick = { showQuitConfirm = false }) {
                                    Text("Cancel")
                                }
                            }
                        )
                    }
                }
            }

            Spacer(Modifier.height(8.dp))

            // Appearance
            Card(modifier = Modifier.fillMaxWidth()) {
                Column(modifier = Modifier.padding(16.dp)) {
                    Text("Appearance", style = MaterialTheme.typography.titleMedium)
                    Spacer(Modifier.height(8.dp))
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.SpaceBetween,
                        verticalAlignment = Alignment.CenterVertically,
                    ) {
                        Text("Dark theme", style = MaterialTheme.typography.bodyMedium)
                        Switch(
                            checked = activity.isDarkTheme,
                            onCheckedChange = { activity.applyDarkTheme(it) }
                        )
                    }
                    if (android.os.Build.VERSION.SDK_INT >= android.os.Build.VERSION_CODES.S) {
                        Spacer(Modifier.height(4.dp))
                        Row(
                            modifier = Modifier.fillMaxWidth(),
                            horizontalArrangement = Arrangement.SpaceBetween,
                            verticalAlignment = Alignment.CenterVertically,
                        ) {
                            Column(modifier = Modifier.weight(1f)) {
                                Text("Material You dynamic color", style = MaterialTheme.typography.bodyMedium)
                                Text(
                                    "Use wallpaper colors (Android 12+)",
                                    style = MaterialTheme.typography.bodySmall,
                                    color = MaterialTheme.colorScheme.onSurfaceVariant
                                )
                            }
                            Switch(
                                checked = activity.useDynamicColor,
                                onCheckedChange = { activity.setDynamicColor(it) }
                            )
                        }
                    }
                }
            }

            Spacer(Modifier.height(8.dp))

            // Local WiFi Hotspot
            val hotspotState by (service?.hotspot?.state?.collectAsState()
                ?: remember { mutableStateOf(com.nexus.mesh.service.HotspotManager.HotspotState.Off) })
            Card(modifier = Modifier.fillMaxWidth()) {
                Column(modifier = Modifier.padding(16.dp)) {
                    Text("Local WiFi Hotspot", style = MaterialTheme.typography.titleMedium)
                    Spacer(Modifier.height(4.dp))
                    Text(
                        "Share NEXUS mesh access with nearby devices over a local WiFi hotspot (no internet required).",
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                    Spacer(Modifier.height(8.dp))
                    val isOn = hotspotState is com.nexus.mesh.service.HotspotManager.HotspotState.On
                    val isStarting = hotspotState is com.nexus.mesh.service.HotspotManager.HotspotState.Starting
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.SpaceBetween,
                        verticalAlignment = Alignment.CenterVertically,
                    ) {
                        Column(modifier = Modifier.weight(1f)) {
                            Text(
                                when (hotspotState) {
                                    is com.nexus.mesh.service.HotspotManager.HotspotState.Off -> "Off"
                                    is com.nexus.mesh.service.HotspotManager.HotspotState.Starting -> "Starting…"
                                    is com.nexus.mesh.service.HotspotManager.HotspotState.On -> {
                                        val info = (hotspotState as com.nexus.mesh.service.HotspotManager.HotspotState.On).info
                                        "On — SSID: ${info.ssid}"
                                    }
                                    is com.nexus.mesh.service.HotspotManager.HotspotState.Failed -> {
                                        val err = (hotspotState as com.nexus.mesh.service.HotspotManager.HotspotState.Failed).reason
                                        "Failed: $err"
                                    }
                                },
                                style = MaterialTheme.typography.bodySmall,
                                color = if (isOn) MaterialTheme.colorScheme.primary
                                        else MaterialTheme.colorScheme.onSurfaceVariant
                            )
                        }
                        Switch(
                            checked = isOn || isStarting,
                            onCheckedChange = { on ->
                                if (on) service?.hotspot?.start() else service?.hotspot?.stop()
                            },
                            enabled = !isStarting
                        )
                    }
                    if (isOn) {
                        val info = (hotspotState as com.nexus.mesh.service.HotspotManager.HotspotState.On).info
                        if (info.passphrase.isNotBlank()) {
                            Spacer(Modifier.height(4.dp))
                            Text(
                                "Password: ${info.passphrase}",
                                style = MaterialTheme.typography.bodySmall,
                                color = MaterialTheme.colorScheme.onSurfaceVariant
                            )
                        }
                    }
                }
            }

            Spacer(Modifier.height(8.dp))

            // About
            Card(modifier = Modifier.fillMaxWidth()) {
                Column(modifier = Modifier.padding(16.dp)) {
                    Text("About", style = MaterialTheme.typography.titleMedium)
                    Spacer(Modifier.height(8.dp))
                    Text("NEXUS Mesh v${BuildConfig.VERSION_NAME}")
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

    if (showAnnouncePicker) {
        val labels = ANNOUNCE_INTERVAL_OPTIONS.map { (label, _) -> label }
        ListPickerDialog(
            title = "Auto-announce every",
            items = labels,
            onSelect = { idx ->
                service?.setAnnounceIntervalMs(ANNOUNCE_INTERVAL_OPTIONS[idx].second)
                showAnnouncePicker = false
            },
            onDismiss = { showAnnouncePicker = false }
        )
    }

    if (showRolePicker) {
        ListPickerDialog(
            title = "Node Role",
            items = ROLE_OPTIONS.map { it.first },
            onSelect = { idx ->
                service?.setRole(ROLE_OPTIONS[idx].second)
                showRolePicker = false
            },
            onDismiss = { showRolePicker = false }
        )
    }
}

private val ROLE_OPTIONS: List<Pair<String, Int>> = listOf(
    "Leaf — endpoint only, never forwards"             to com.nexus.mesh.service.NexusNode.ROLE_LEAF,
    "Relay — forwards + small store-and-forward"       to com.nexus.mesh.service.NexusNode.ROLE_RELAY,
    "Gateway — bridges across transports"              to com.nexus.mesh.service.NexusNode.ROLE_GATEWAY,
)

private fun roleLabel(role: Int): String =
    ROLE_OPTIONS.firstOrNull { it.second == role }?.first
        ?: "Role $role"

private val ANNOUNCE_INTERVAL_OPTIONS: List<Pair<String, Long>> = listOf(
    "Off (manual only)" to 0L,
    "10 seconds"        to 10_000L,
    "30 seconds"        to 30_000L,
    "1 minute"          to 60_000L,
    "5 minutes"         to 5L * 60_000L,
    "15 minutes"        to 15L * 60_000L,
    "30 minutes"        to 30L * 60_000L,
)

private fun formatAnnounceInterval(ms: Long): String {
    if (ms <= 0L) return "Off"
    return ANNOUNCE_INTERVAL_OPTIONS.firstOrNull { it.second == ms }?.first
        ?: when {
            ms < 60_000L  -> "${ms / 1000} s"
            ms < 3_600_000L -> "${ms / 60_000L} min"
            else -> "${ms / 3_600_000L} h"
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
