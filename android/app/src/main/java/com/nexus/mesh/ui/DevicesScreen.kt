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

@Composable
fun DevicesScreen(activity: MainActivity) {
    val bleTransport = remember { BleTransport(activity) }
    val devices by bleTransport.devices.collectAsState()
    val connected by bleTransport.connected.collectAsState()
    val connectedDevice by bleTransport.connectedDevice.collectAsState()
    var scanning by remember { mutableStateOf(false) }

    Column(
        modifier = Modifier.fillMaxSize().padding(16.dp)
    ) {
        Text("BLE Devices", style = MaterialTheme.typography.headlineMedium)
        Spacer(Modifier.height(8.dp))

        // Connection status
        if (connected) {
            Card(
                modifier = Modifier.fillMaxWidth(),
                colors = CardDefaults.cardColors(
                    containerColor = MaterialTheme.colorScheme.primaryContainer
                )
            ) {
                Row(
                    modifier = Modifier.padding(16.dp).fillMaxWidth(),
                    horizontalArrangement = Arrangement.SpaceBetween,
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Column {
                        Text("Connected", style = MaterialTheme.typography.titleMedium)
                        Text(connectedDevice ?: "", style = MaterialTheme.typography.bodySmall)
                    }
                    Button(onClick = { bleTransport.disconnect() }) {
                        Text("Disconnect")
                    }
                }
            }
        }

        Spacer(Modifier.height(16.dp))

        // Scan button
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

        Spacer(Modifier.height(16.dp))

        if (devices.isEmpty() && scanning) {
            Text(
                "Scanning for NEXUS devices...",
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
        }

        // Device list
        LazyColumn {
            items(devices) { device ->
                Card(
                    modifier = Modifier
                        .fillMaxWidth()
                        .padding(vertical = 4.dp)
                        .clickable {
                            bleTransport.connect(device.address)
                            scanning = false
                        }
                ) {
                    Row(
                        modifier = Modifier.padding(16.dp).fillMaxWidth(),
                        horizontalArrangement = Arrangement.SpaceBetween
                    ) {
                        Column {
                            Text(device.name, style = MaterialTheme.typography.titleSmall)
                            Text(
                                device.address,
                                style = MaterialTheme.typography.bodySmall,
                                color = MaterialTheme.colorScheme.onSurfaceVariant
                            )
                        }
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
