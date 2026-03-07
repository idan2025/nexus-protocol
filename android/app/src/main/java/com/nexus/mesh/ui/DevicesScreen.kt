package com.nexus.mesh.ui

import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import com.nexus.mesh.ble.BleTransport

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun DevicesScreen(activity: MainActivity) {
    val bleTransport = remember { BleTransport(activity) }
    val devices by bleTransport.devices.collectAsState()
    val connected by bleTransport.connected.collectAsState()
    val connectedDevice by bleTransport.connectedDevice.collectAsState()
    var scanning by remember { mutableStateOf(false) }

    Scaffold(
        topBar = {
            TopAppBar(title = { Text("BLE Devices") })
        }
    ) { padding ->
        LazyColumn(
            modifier = Modifier
                .fillMaxSize()
                .padding(padding),
            contentPadding = PaddingValues(16.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            // Connection status
            if (connected) {
                item {
                    Card(
                        modifier = Modifier.fillMaxWidth(),
                        colors = CardDefaults.cardColors(
                            containerColor = MaterialTheme.colorScheme.primaryContainer
                        )
                    ) {
                        Row(
                            modifier = Modifier
                                .fillMaxWidth()
                                .padding(16.dp),
                            horizontalArrangement = Arrangement.SpaceBetween,
                            verticalAlignment = Alignment.CenterVertically
                        ) {
                            Column(modifier = Modifier.weight(1f)) {
                                Text("Connected", style = MaterialTheme.typography.titleMedium)
                                Text(
                                    connectedDevice ?: "",
                                    style = MaterialTheme.typography.bodySmall
                                )
                            }
                            Spacer(Modifier.width(8.dp))
                            Button(onClick = { bleTransport.disconnect() }) {
                                Text("Disconnect")
                            }
                        }
                    }
                }
            }

            // Scan button
            item {
                Button(
                    onClick = {
                        if (scanning) {
                            bleTransport.stopScan()
                            scanning = false
                        } else {
                            bleTransport.startScan()
                            scanning = true
                        }
                    },
                    modifier = Modifier.fillMaxWidth()
                ) {
                    Text(if (scanning) "Stop Scanning" else "Scan for NEXUS Devices")
                }
            }

            if (devices.isEmpty() && scanning) {
                item {
                    Text(
                        "Scanning for NEXUS devices...",
                        style = MaterialTheme.typography.bodyMedium,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                }
            }

            // Device list
            items(devices) { device ->
                Card(
                    modifier = Modifier
                        .fillMaxWidth()
                        .clickable {
                            bleTransport.connect(device.address)
                            scanning = false
                        }
                ) {
                    Row(
                        modifier = Modifier
                            .fillMaxWidth()
                            .padding(16.dp),
                        horizontalArrangement = Arrangement.SpaceBetween,
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        Column(modifier = Modifier.weight(1f)) {
                            Text(device.name, style = MaterialTheme.typography.titleSmall)
                            Text(
                                device.address,
                                style = MaterialTheme.typography.bodySmall,
                                color = MaterialTheme.colorScheme.onSurfaceVariant
                            )
                        }
                        Spacer(Modifier.width(8.dp))
                        Text(
                            "${device.rssi} dBm",
                            color = MaterialTheme.colorScheme.secondary
                        )
                    }
                }
            }
        }
    }
}
