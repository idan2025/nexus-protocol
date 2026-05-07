package com.nexus.mesh.ui

import android.content.Intent
import android.net.Uri
import android.os.Build
import android.widget.Toast
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Bluetooth
import androidx.compose.material.icons.filled.Download
import androidx.compose.material.icons.filled.Memory
import androidx.compose.material.icons.filled.OpenInBrowser
import androidx.compose.material.icons.filled.Usb
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.core.content.FileProvider
import com.nexus.mesh.flasher.BleDfuFlasher
import com.nexus.mesh.flasher.BoardSpec
import com.nexus.mesh.flasher.FirmwareCatalog
import com.nexus.mesh.flasher.FirmwareDownloader
import com.nexus.mesh.flasher.UsbFlasher
import kotlinx.coroutines.launch

/**
 * Hub screen for flashing firmware onto a node.
 *
 * Shows one card per supported board with three possible actions:
 *   - Download .bin / .uf2 / .zip from the latest GitHub release
 *   - For ESP32 boards: USB-OTG flash (cable to phone)
 *   - For nRF52 boards: BLE-DFU OTA via the currently-connected node
 *   - For all: open the web flasher in a browser tab (desktop only)
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun FlashNodeScreen(activity: MainActivity) {
    val ctx = activity
    val downloader = remember { FirmwareDownloader(ctx) }
    val usbFlasher = remember { UsbFlasher(ctx) }
    val bleDfuFlasher = remember { BleDfuFlasher(ctx) }
    val scope = rememberCoroutineScope()

    var latestTag by remember { mutableStateOf<String?>(null) }
    var loadingTag by remember { mutableStateOf(true) }

    LaunchedEffect(Unit) {
        latestTag = downloader.latestTag()
        loadingTag = false
    }
    DisposableEffect(Unit) { onDispose { bleDfuFlasher.release() } }

    val downloadProgress by downloader.progress.collectAsState()
    val usbState by usbFlasher.state.collectAsState()
    val dfuState by bleDfuFlasher.state.collectAsState()

    Scaffold(topBar = { TopAppBar(title = { Text("Flash Node") }) }) { padding ->
        LazyColumn(
            modifier = Modifier.fillMaxSize().padding(padding),
            contentPadding = PaddingValues(12.dp),
            verticalArrangement = Arrangement.spacedBy(10.dp)
        ) {
            item {
                Card(modifier = Modifier.fillMaxWidth()) {
                    Column(modifier = Modifier.padding(14.dp)) {
                        Text("Latest release",
                             fontWeight = FontWeight.SemiBold,
                             style = MaterialTheme.typography.bodyMedium)
                        Text(
                            when {
                                loadingTag -> "Resolving..."
                                latestTag == null -> "Could not reach GitHub. Check connectivity."
                                else -> latestTag!!
                            },
                            style = MaterialTheme.typography.bodyLarge,
                            color = MaterialTheme.colorScheme.primary
                        )
                        Text(
                            "Each board fetches its own merged image " +
                            "from this release.",
                            style = MaterialTheme.typography.bodySmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                    }
                }
            }

            items(FirmwareCatalog.BOARDS) { board ->
                BoardFlashCard(
                    board = board,
                    tag = latestTag,
                    activity = activity,
                    downloader = downloader,
                    usbFlasher = usbFlasher,
                    bleDfuFlasher = bleDfuFlasher,
                    downloadProgress = downloadProgress,
                    usbState = usbState,
                    dfuState = dfuState,
                    scope = scope,
                )
            }

            item {
                Spacer(Modifier.height(16.dp))
                Text(
                    "Note: Web flashing requires desktop Chrome/Edge — " +
                    "Android browsers don't support WebSerial.",
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                    modifier = Modifier.padding(horizontal = 12.dp, vertical = 4.dp)
                )
            }
        }
    }
}

@Composable
private fun BoardFlashCard(
    board: BoardSpec,
    tag: String?,
    activity: MainActivity,
    downloader: FirmwareDownloader,
    usbFlasher: UsbFlasher,
    bleDfuFlasher: BleDfuFlasher,
    downloadProgress: FirmwareDownloader.Progress,
    usbState: UsbFlasher.State,
    dfuState: BleDfuFlasher.State,
    scope: kotlinx.coroutines.CoroutineScope,
) {
    val ctx = activity
    Card(modifier = Modifier.fillMaxWidth()) {
        Column(modifier = Modifier.padding(14.dp)) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Icon(Icons.Default.Memory, contentDescription = null,
                     tint = MaterialTheme.colorScheme.primary)
                Spacer(Modifier.width(8.dp))
                Column(modifier = Modifier.weight(1f)) {
                    Text(board.displayName, fontWeight = FontWeight.SemiBold)
                    Text(board.mcu, style = MaterialTheme.typography.bodySmall,
                         color = MaterialTheme.colorScheme.onSurfaceVariant)
                }
                Surface(
                    shape = RoundedCornerShape(6.dp),
                    color = if (board.hasUsbFlash)
                        MaterialTheme.colorScheme.tertiaryContainer
                    else MaterialTheme.colorScheme.secondaryContainer,
                    modifier = Modifier
                ) {
                    Text(
                        if (board.hasUsbFlash) "USB" else "BLE-DFU",
                        modifier = Modifier.padding(horizontal = 6.dp, vertical = 2.dp),
                        style = MaterialTheme.typography.labelSmall
                    )
                }
            }

            Spacer(Modifier.height(8.dp))
            Text(board.tagline, style = MaterialTheme.typography.bodySmall)

            Spacer(Modifier.height(12.dp))

            // Download row
            val primaryAsset =
                if (board.hasUsbFlash) board.webflashAsset
                else board.dfuZipAsset ?: board.uf2Asset

            if (primaryAsset != null) {
                Button(
                    onClick = {
                        tag?.let { t ->
                            scope.launch { downloader.fetch(t, primaryAsset) }
                        }
                    },
                    enabled = tag != null,
                    modifier = Modifier.fillMaxWidth()
                ) {
                    Icon(Icons.Default.Download, contentDescription = null)
                    Spacer(Modifier.width(8.dp))
                    Text("Download $primaryAsset")
                }
                when (val p = downloadProgress) {
                    is FirmwareDownloader.Progress.Downloading ->
                        if (p.asset == primaryAsset) {
                            LinearProgressIndicator(
                                progress = { p.pct / 100f },
                                modifier = Modifier.fillMaxWidth().padding(top = 6.dp)
                            )
                        }
                    is FirmwareDownloader.Progress.Done ->
                        if (p.asset == primaryAsset) {
                            Text("Downloaded: ${p.file.name} (${p.file.length() / 1024} KB)",
                                 style = MaterialTheme.typography.bodySmall,
                                 color = MaterialTheme.colorScheme.primary,
                                 modifier = Modifier.padding(top = 4.dp))
                        }
                    is FirmwareDownloader.Progress.Error ->
                        if (p.asset == primaryAsset) {
                            Text("Error: ${p.msg}", color = MaterialTheme.colorScheme.error,
                                 style = MaterialTheme.typography.bodySmall)
                        }
                    else -> Unit
                }
            }

            Spacer(Modifier.height(8.dp))

            // ESP32 boards: USB-OTG flash
            if (board.hasUsbFlash) {
                OutlinedButton(
                    onClick = {
                        scope.launch {
                            val drivers = usbFlasher.enumerate()
                            if (drivers.isEmpty()) {
                                Toast.makeText(ctx, "No USB serial device. Plug in via OTG cable.", Toast.LENGTH_LONG).show()
                                return@launch
                            }
                            val driver = drivers.first()
                            if (!usbFlasher.requestPermission(driver)) {
                                Toast.makeText(ctx, "USB permission denied", Toast.LENGTH_SHORT).show()
                                return@launch
                            }
                            val t = tag ?: return@launch
                            val asset = board.webflashAsset ?: return@launch
                            val file = downloader.fetch(t, asset) ?: run {
                                Toast.makeText(ctx, "Download failed", Toast.LENGTH_SHORT).show()
                                return@launch
                            }
                            usbFlasher.flash(driver, file, offset = 0)
                        }
                    },
                    modifier = Modifier.fillMaxWidth()
                ) {
                    Icon(Icons.Default.Usb, contentDescription = null)
                    Spacer(Modifier.width(8.dp))
                    Text("Flash via USB OTG cable")
                }
                UsbFlashStatusLine(usbState)
            }

            // nRF52 boards: BLE-DFU
            if (board.hasBleDfu) {
                OutlinedButton(
                    onClick = onClick@ {
                        val nodeAddr = activity.bleConnectedAddress()
                        if (nodeAddr == null) {
                            Toast.makeText(ctx,
                                "Connect to a ${board.displayName} via BLE first (Devices screen).",
                                Toast.LENGTH_LONG).show()
                            return@onClick
                        }
                        val t = tag ?: return@onClick
                        val asset = board.dfuZipAsset ?: return@onClick
                        scope.launch {
                            val zip = downloader.fetch(t, asset) ?: run {
                                Toast.makeText(ctx, "DFU zip not found in release",
                                    Toast.LENGTH_LONG).show()
                                return@launch
                            }
                            bleDfuFlasher.start(nodeAddr, board.displayName, zip)
                        }
                    },
                    modifier = Modifier.fillMaxWidth()
                ) {
                    Icon(Icons.Default.Bluetooth, contentDescription = null)
                    Spacer(Modifier.width(8.dp))
                    Text("Flash this node via BLE DFU")
                }
                BleDfuStatusLine(dfuState)
            }

            // Web flasher launcher
            TextButton(
                onClick = {
                    val url = "https://idan2025.github.io/nexus-protocol/"
                    val intent = Intent(Intent.ACTION_VIEW, Uri.parse(url))
                    activity.startActivity(intent)
                },
                modifier = Modifier.fillMaxWidth()
            ) {
                Icon(Icons.Default.OpenInBrowser, contentDescription = null)
                Spacer(Modifier.width(6.dp))
                Text("Open web flasher in browser")
            }
        }
    }
}

@Composable
private fun UsbFlashStatusLine(s: UsbFlasher.State) {
    when (s) {
        is UsbFlasher.State.Connecting -> Text("Connecting...", style = MaterialTheme.typography.bodySmall)
        is UsbFlasher.State.Syncing -> Text("Syncing with ROM bootloader...", style = MaterialTheme.typography.bodySmall)
        is UsbFlasher.State.Flashing -> {
            LinearProgressIndicator(
                progress = { s.block.toFloat() / s.total.coerceAtLeast(1) },
                modifier = Modifier.fillMaxWidth().padding(top = 4.dp)
            )
            Text("Block ${s.block}/${s.total}", style = MaterialTheme.typography.bodySmall)
        }
        is UsbFlasher.State.Done -> Text("Flash complete. Node is rebooting.",
                                         style = MaterialTheme.typography.bodySmall,
                                         color = MaterialTheme.colorScheme.primary)
        is UsbFlasher.State.Error -> Text(s.msg, color = MaterialTheme.colorScheme.error,
                                          style = MaterialTheme.typography.bodySmall)
        else -> Unit
    }
}

@Composable
private fun BleDfuStatusLine(s: BleDfuFlasher.State) {
    when (s) {
        is BleDfuFlasher.State.Connecting -> Text("Connecting to ${s.deviceAddress}...", style = MaterialTheme.typography.bodySmall)
        is BleDfuFlasher.State.EnablingDfu -> Text("Entering DFU mode (${s.pct}%)...", style = MaterialTheme.typography.bodySmall)
        is BleDfuFlasher.State.Uploading -> {
            LinearProgressIndicator(
                progress = { s.pct / 100f },
                modifier = Modifier.fillMaxWidth().padding(top = 4.dp)
            )
            Text("Uploading ${s.pct}%  (${"%.1f".format(s.avgSpeedBps / 1024)} KB/s)",
                 style = MaterialTheme.typography.bodySmall)
        }
        is BleDfuFlasher.State.Validating -> Text("Validating image...", style = MaterialTheme.typography.bodySmall)
        is BleDfuFlasher.State.Done -> Text("DFU complete. Node has rebooted.",
                                            style = MaterialTheme.typography.bodySmall,
                                            color = MaterialTheme.colorScheme.primary)
        is BleDfuFlasher.State.Error -> Text("DFU error ${s.code}: ${s.msg}",
                                             color = MaterialTheme.colorScheme.error,
                                             style = MaterialTheme.typography.bodySmall)
        else -> Unit
    }
}
