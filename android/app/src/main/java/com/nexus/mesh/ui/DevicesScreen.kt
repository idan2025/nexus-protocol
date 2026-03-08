package com.nexus.mesh.ui

import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import com.nexus.mesh.ble.BleTransport
import kotlin.math.roundToInt
import kotlin.math.roundToLong

/* ── Region Presets ──────────────────────────────────────────────────── */

data class RegionPreset(
    val name: String,
    val frequencyHz: Long,
    val bandwidthHz: Long,
    val txPowerDbm: Int
)

val REGION_PRESETS = listOf(
    RegionPreset("US 915 MHz",  915000000L, 250000L, 17),
    RegionPreset("EU 868 MHz",  868000000L, 125000L, 14),
    RegionPreset("AU 915 MHz",  915200000L, 250000L, 17),
    RegionPreset("AS 923 MHz",  923000000L, 125000L, 14),
    RegionPreset("KR 920 MHz",  920900000L, 125000L, 14),
    RegionPreset("IN 865 MHz",  865400000L, 125000L, 14),
)

/* ── Modem Presets ──────────────────────────────────────────────────── */

data class ModemPreset(
    val name: String,
    val spreadingFactor: Int,
    val bandwidthHz: Long,
    val codingRate: Int
)

val MODEM_PRESETS = listOf(
    ModemPreset("Long Range Slow",     12, 125000L, 8),
    ModemPreset("Long Range Moderate", 11, 125000L, 5),
    ModemPreset("Long Range Fast",     11, 250000L, 5),
    ModemPreset("Medium (Default)",     9, 250000L, 5),
    ModemPreset("Short Range Fast",     7, 250000L, 5),
    ModemPreset("Short Range Turbo",    7, 500000L, 5),
)

/* ── Role Names ─────────────────────────────────────────────────────── */

val ROLE_NAMES = listOf(
    "Leaf", "Relay", "Gateway", "Anchor", "Sentinel", "Pillar", "Vault"
)

/* ── Screen Timeout Options ─────────────────────────────────────────── */

data class TimeoutOption(val name: String, val ms: Long)

val TIMEOUT_OPTIONS = listOf(
    TimeoutOption("15 seconds",  15000L),
    TimeoutOption("30 seconds",  30000L),
    TimeoutOption("1 minute",    60000L),
    TimeoutOption("2 minutes",  120000L),
    TimeoutOption("5 minutes",  300000L),
    TimeoutOption("10 minutes", 600000L),
    TimeoutOption("Never",            0L),
)

/* ── Helpers ─────────────────────────────────────────────────────────── */

fun formatFrequency(hz: Long): String {
    val mhz = hz / 1_000_000.0
    return "%.3f MHz".format(mhz)
}

fun formatBandwidth(hz: Long): String {
    return when {
        hz >= 1_000_000 -> "${hz / 1_000_000} MHz"
        hz >= 1_000 -> "${hz / 1_000} kHz"
        else -> "$hz Hz"
    }
}

fun formatTimeout(ms: Long): String {
    if (ms == 0L) return "Never"
    val sec = ms / 1000
    return when {
        sec >= 60 -> "${sec / 60} min"
        else -> "${sec}s"
    }
}

/* ── Main Screen ─────────────────────────────────────────────────────── */

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun DevicesScreen(activity: MainActivity) {
    val bleTransport = remember { BleTransport(activity) }
    val devices by bleTransport.devices.collectAsState()
    val connected by bleTransport.connected.collectAsState()
    val connectedDevice by bleTransport.connectedDevice.collectAsState()
    val nodeConfig by bleTransport.nodeConfig.collectAsState()
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

                // Node Settings (only when connected and config received)
                item {
                    NodeSettingsCard(bleTransport, nodeConfig)
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

/* ── Node Settings Card ─────────────────────────────────────────────── */

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun NodeSettingsCard(ble: BleTransport, config: BleTransport.NodeConfig?) {
    var showRadioSheet by remember { mutableStateOf(false) }
    var showRegionPicker by remember { mutableStateOf(false) }
    var showModemPicker by remember { mutableStateOf(false) }
    var showTimeoutPicker by remember { mutableStateOf(false) }
    var showRolePicker by remember { mutableStateOf(false) }
    var showAdvanced by remember { mutableStateOf(false) }
    var showRebootConfirm by remember { mutableStateOf(false) }

    Card(modifier = Modifier.fillMaxWidth()) {
        Column(modifier = Modifier.padding(16.dp)) {
            Text("Node Settings", style = MaterialTheme.typography.titleMedium)
            Spacer(Modifier.height(4.dp))
            Text(
                "Configure your NEXUS node remotely via BLE",
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
            Spacer(Modifier.height(12.dp))

            if (config == null) {
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.Center
                ) {
                    CircularProgressIndicator(modifier = Modifier.size(24.dp))
                    Spacer(Modifier.width(12.dp))
                    Text("Loading config...", style = MaterialTheme.typography.bodyMedium)
                }
                Spacer(Modifier.height(8.dp))
                Button(
                    onClick = { ble.requestConfig() },
                    modifier = Modifier.fillMaxWidth()
                ) {
                    Text("Refresh Config")
                }
                return@Column
            }

            // Node info row
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween
            ) {
                Text("Address", style = MaterialTheme.typography.bodyMedium)
                Text(config.nodeAddr, color = MaterialTheme.colorScheme.primary,
                     fontWeight = FontWeight.Bold)
            }
            Spacer(Modifier.height(8.dp))

            Divider()
            Spacer(Modifier.height(8.dp))

            // Region preset
            SettingsRow(
                label = "Region",
                value = REGION_PRESETS.find { it.frequencyHz == config.frequencyHz }?.name
                    ?: formatFrequency(config.frequencyHz),
                onClick = { showRegionPicker = true }
            )

            // Modem preset
            SettingsRow(
                label = "Modem",
                value = MODEM_PRESETS.find {
                    it.spreadingFactor == config.spreadingFactor &&
                    it.bandwidthHz == config.bandwidthHz &&
                    it.codingRate == config.codingRate
                }?.name ?: "Custom",
                onClick = { showModemPicker = true }
            )

            // Screen timeout
            SettingsRow(
                label = "Screen Timeout",
                value = TIMEOUT_OPTIONS.find { it.ms == config.screenTimeoutMs }?.name
                    ?: formatTimeout(config.screenTimeoutMs),
                onClick = { showTimeoutPicker = true }
            )

            // Role
            SettingsRow(
                label = "Node Role",
                value = ROLE_NAMES.getOrElse(config.nodeRole) { "Unknown" },
                onClick = { showRolePicker = true }
            )

            Spacer(Modifier.height(4.dp))
            Divider()
            Spacer(Modifier.height(8.dp))

            // Radio details (collapsed by default)
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .clickable { showAdvanced = !showAdvanced },
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically
            ) {
                Text("Radio Details", style = MaterialTheme.typography.labelMedium)
                Text(
                    if (showAdvanced) "Hide" else "Show",
                    style = MaterialTheme.typography.labelSmall,
                    color = MaterialTheme.colorScheme.primary
                )
            }
            if (showAdvanced) {
                Spacer(Modifier.height(8.dp))
                RadioDetailRow("Frequency", formatFrequency(config.frequencyHz))
                RadioDetailRow("Bandwidth", formatBandwidth(config.bandwidthHz))
                RadioDetailRow("Spreading Factor", "SF${config.spreadingFactor}")
                RadioDetailRow("Coding Rate", "4/${config.codingRate}")
                RadioDetailRow("TX Power", "${config.txPowerDbm} dBm")
                Spacer(Modifier.height(8.dp))
                Button(
                    onClick = { showRadioSheet = true },
                    modifier = Modifier.fillMaxWidth()
                ) {
                    Text("Manual Radio Config")
                }
            }

            Spacer(Modifier.height(12.dp))

            // Action buttons
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.spacedBy(8.dp)
            ) {
                OutlinedButton(
                    onClick = { ble.requestConfig() },
                    modifier = Modifier.weight(1f)
                ) {
                    Text("Refresh")
                }
                OutlinedButton(
                    onClick = { showRebootConfirm = true },
                    modifier = Modifier.weight(1f),
                    colors = ButtonDefaults.outlinedButtonColors(
                        contentColor = MaterialTheme.colorScheme.error
                    )
                ) {
                    Text("Reboot")
                }
            }
        }
    }

    // Pickers
    if (showRegionPicker) {
        ListPickerDialog(
            title = "Select Region",
            items = REGION_PRESETS.map { it.name },
            onSelect = { idx ->
                val preset = REGION_PRESETS[idx]
                ble.setRadioConfig(
                    preset.frequencyHz, preset.bandwidthHz,
                    config?.spreadingFactor ?: 9,
                    config?.codingRate ?: 5,
                    preset.txPowerDbm
                )
                showRegionPicker = false
            },
            onDismiss = { showRegionPicker = false }
        )
    }

    if (showModemPicker) {
        ListPickerDialog(
            title = "Select Modem Preset",
            items = MODEM_PRESETS.map { it.name },
            onSelect = { idx ->
                val preset = MODEM_PRESETS[idx]
                ble.setRadioConfig(
                    config?.frequencyHz ?: 915000000L,
                    preset.bandwidthHz,
                    preset.spreadingFactor,
                    preset.codingRate,
                    config?.txPowerDbm ?: 17
                )
                showModemPicker = false
            },
            onDismiss = { showModemPicker = false }
        )
    }

    if (showTimeoutPicker) {
        ListPickerDialog(
            title = "Screen Timeout",
            items = TIMEOUT_OPTIONS.map { it.name },
            onSelect = { idx ->
                ble.setScreenTimeout(TIMEOUT_OPTIONS[idx].ms)
                showTimeoutPicker = false
            },
            onDismiss = { showTimeoutPicker = false }
        )
    }

    if (showRolePicker) {
        ListPickerDialog(
            title = "Node Role",
            items = ROLE_NAMES,
            onSelect = { idx ->
                ble.setNodeRole(idx)
                showRolePicker = false
            },
            onDismiss = { showRolePicker = false }
        )
    }

    if (showRadioSheet && config != null) {
        ManualRadioDialog(
            config = config,
            onApply = { freq, bw, sf, cr, pwr ->
                ble.setRadioConfig(freq, bw, sf, cr, pwr)
                showRadioSheet = false
            },
            onDismiss = { showRadioSheet = false }
        )
    }

    if (showRebootConfirm) {
        AlertDialog(
            onDismissRequest = { showRebootConfirm = false },
            title = { Text("Reboot Node?") },
            text = { Text("This will restart the NEXUS node. Settings are saved automatically.") },
            confirmButton = {
                TextButton(onClick = {
                    ble.rebootDevice()
                    showRebootConfirm = false
                }) {
                    Text("Reboot", color = MaterialTheme.colorScheme.error)
                }
            },
            dismissButton = {
                TextButton(onClick = { showRebootConfirm = false }) {
                    Text("Cancel")
                }
            }
        )
    }
}

/* ── Settings Row ───────────────────────────────────────────────────── */

@Composable
fun SettingsRow(label: String, value: String, onClick: () -> Unit) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .clickable(onClick = onClick)
            .padding(vertical = 8.dp),
        horizontalArrangement = Arrangement.SpaceBetween,
        verticalAlignment = Alignment.CenterVertically
    ) {
        Text(label, style = MaterialTheme.typography.bodyMedium)
        Text(
            value,
            style = MaterialTheme.typography.bodyMedium,
            color = MaterialTheme.colorScheme.primary
        )
    }
}

@Composable
fun RadioDetailRow(label: String, value: String) {
    Row(
        modifier = Modifier.fillMaxWidth().padding(vertical = 2.dp),
        horizontalArrangement = Arrangement.SpaceBetween
    ) {
        Text(label, style = MaterialTheme.typography.bodySmall,
             color = MaterialTheme.colorScheme.onSurfaceVariant)
        Text(value, style = MaterialTheme.typography.bodySmall)
    }
}

/* ── List Picker Dialog ─────────────────────────────────────────────── */

@Composable
fun ListPickerDialog(
    title: String,
    items: List<String>,
    onSelect: (Int) -> Unit,
    onDismiss: () -> Unit
) {
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text(title) },
        text = {
            Column {
                items.forEachIndexed { idx, item ->
                    TextButton(
                        onClick = { onSelect(idx) },
                        modifier = Modifier.fillMaxWidth()
                    ) {
                        Text(item, modifier = Modifier.fillMaxWidth())
                    }
                }
            }
        },
        confirmButton = {
            TextButton(onClick = onDismiss) {
                Text("Cancel")
            }
        }
    )
}

/* ── Manual Radio Config Dialog ─────────────────────────────────────── */

@Composable
fun ManualRadioDialog(
    config: BleTransport.NodeConfig,
    onApply: (Long, Long, Int, Int, Int) -> Unit,
    onDismiss: () -> Unit
) {
    var freqMhz by remember { mutableStateOf("%.3f".format(config.frequencyHz / 1_000_000.0)) }
    var sfValue by remember { mutableStateOf(config.spreadingFactor.toFloat()) }
    var crValue by remember { mutableStateOf(config.codingRate.toFloat()) }
    var pwrValue by remember { mutableStateOf(config.txPowerDbm.toFloat()) }
    var bwIndex by remember {
        val bws = listOf(125000L, 250000L, 500000L)
        mutableStateOf(bws.indexOf(config.bandwidthHz).coerceAtLeast(0).toFloat())
    }
    val bwOptions = listOf(125000L, 250000L, 500000L)
    val bwNames = listOf("125 kHz", "250 kHz", "500 kHz")

    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text("Manual Radio Config") },
        text = {
            Column {
                OutlinedTextField(
                    value = freqMhz,
                    onValueChange = { freqMhz = it },
                    label = { Text("Frequency (MHz)") },
                    singleLine = true,
                    modifier = Modifier.fillMaxWidth()
                )
                Spacer(Modifier.height(12.dp))

                Text("Bandwidth: ${bwNames[bwIndex.roundToInt()]}",
                     style = MaterialTheme.typography.bodyMedium)
                Slider(
                    value = bwIndex,
                    onValueChange = { bwIndex = it },
                    valueRange = 0f..2f,
                    steps = 1
                )

                Text("Spreading Factor: SF${sfValue.roundToInt()}",
                     style = MaterialTheme.typography.bodyMedium)
                Slider(
                    value = sfValue,
                    onValueChange = { sfValue = it },
                    valueRange = 7f..12f,
                    steps = 4
                )

                Text("Coding Rate: 4/${crValue.roundToInt()}",
                     style = MaterialTheme.typography.bodyMedium)
                Slider(
                    value = crValue,
                    onValueChange = { crValue = it },
                    valueRange = 5f..8f,
                    steps = 2
                )

                Text("TX Power: ${pwrValue.roundToInt()} dBm",
                     style = MaterialTheme.typography.bodyMedium)
                Slider(
                    value = pwrValue,
                    onValueChange = { pwrValue = it },
                    valueRange = 2f..22f,
                    steps = 19
                )
            }
        },
        confirmButton = {
            TextButton(onClick = {
                val freq = (freqMhz.toDoubleOrNull() ?: 915.0) * 1_000_000.0
                onApply(
                    freq.roundToLong(),
                    bwOptions[bwIndex.roundToInt()],
                    sfValue.roundToInt(),
                    crValue.roundToInt(),
                    pwrValue.roundToInt()
                )
            }) {
                Text("Apply")
            }
        },
        dismissButton = {
            TextButton(onClick = onDismiss) {
                Text("Cancel")
            }
        }
    )
}
