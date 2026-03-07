package com.nexus.mesh.ui

import androidx.compose.foundation.layout.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp

@Composable
fun SettingsScreen(activity: MainActivity) {
    val service = activity.getService()
    val address by service?.address?.collectAsState() ?: remember { mutableStateOf("--------") }
    val tcpActive by service?.tcpActive?.collectAsState() ?: remember { mutableStateOf(false) }
    val udpActive by service?.udpActive?.collectAsState() ?: remember { mutableStateOf(false) }

    val tcpConfig = service?.getTcpConfig()
    var tcpPort by remember { mutableStateOf(tcpConfig?.first?.toString() ?: "4242") }
    var tcpPeers by remember { mutableStateOf(tcpConfig?.second ?: "") }

    Column(
        modifier = Modifier.fillMaxSize().padding(16.dp)
    ) {
        Text("Settings", style = MaterialTheme.typography.headlineMedium)
        Spacer(Modifier.height(16.dp))

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
            }
        }

        Spacer(Modifier.height(16.dp))

        // TCP Internet Transport
        Card(modifier = Modifier.fillMaxWidth()) {
            Column(modifier = Modifier.padding(16.dp)) {
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.SpaceBetween
                ) {
                    Text("TCP Internet", style = MaterialTheme.typography.titleMedium)
                    Text(
                        if (tcpActive) "Active" else "Inactive",
                        color = if (tcpActive) MaterialTheme.colorScheme.primary
                                else MaterialTheme.colorScheme.onSurfaceVariant
                    )
                }
                Spacer(Modifier.height(8.dp))

                OutlinedTextField(
                    value = tcpPort,
                    onValueChange = { tcpPort = it.filter { c -> c.isDigit() } },
                    label = { Text("Listen Port") },
                    modifier = Modifier.fillMaxWidth(),
                    singleLine = true
                )
                Spacer(Modifier.height(8.dp))

                OutlinedTextField(
                    value = tcpPeers,
                    onValueChange = { tcpPeers = it },
                    label = { Text("Peers (host:port, comma separated)") },
                    modifier = Modifier.fillMaxWidth(),
                    singleLine = true
                )
                Spacer(Modifier.height(8.dp))

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

        Spacer(Modifier.height(16.dp))

        // UDP Multicast (LAN Discovery)
        Card(modifier = Modifier.fillMaxWidth()) {
            Column(modifier = Modifier.padding(16.dp)) {
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.SpaceBetween
                ) {
                    Text("LAN Discovery (UDP Multicast)", style = MaterialTheme.typography.titleMedium)
                    Text(
                        if (udpActive) "Active" else "Inactive",
                        color = if (udpActive) MaterialTheme.colorScheme.primary
                                else MaterialTheme.colorScheme.onSurfaceVariant
                    )
                }
                Spacer(Modifier.height(8.dp))
                Text(
                    "Auto-discovers NEXUS nodes on all local network interfaces (WiFi, Ethernet, VPN)",
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
                Spacer(Modifier.height(8.dp))
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.spacedBy(8.dp)
                ) {
                    Button(
                        onClick = { service?.startUdpMulticast() },
                        enabled = !udpActive,
                        modifier = Modifier.weight(1f)
                    ) {
                        Text("Enable")
                    }
                    OutlinedButton(
                        onClick = { service?.stopUdpMulticast() },
                        enabled = udpActive,
                        modifier = Modifier.weight(1f)
                    ) {
                        Text("Disable")
                    }
                }
            }
        }

        Spacer(Modifier.height(16.dp))

        // About
        Card(modifier = Modifier.fillMaxWidth()) {
            Column(modifier = Modifier.padding(16.dp)) {
                Text("About", style = MaterialTheme.typography.titleMedium)
                Spacer(Modifier.height(8.dp))
                Text("NEXUS Mesh v0.1.0")
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
            }
        }
    }
}
