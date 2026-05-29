package com.nexus.mesh.ui

import android.net.Uri
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.filled.Delete
import androidx.compose.material.icons.filled.Download
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import androidx.navigation.NavController
import com.nexus.mesh.maps.MbTilesManager
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.io.File

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun OfflineMapScreen(navController: NavController) {
    val context = LocalContext.current
    val scope = rememberCoroutineScope()

    var archives by remember { mutableStateOf(MbTilesManager.listArchives(context)) }
    var importing by remember { mutableStateOf(false) }
    var downloadProgress by remember { mutableStateOf<Pair<Int, Int>?>(null) }
    var showDownloadDialog by remember { mutableStateOf(false) }
    var statusMsg by remember { mutableStateOf<String?>(null) }

    fun refresh() { archives = MbTilesManager.listArchives(context) }

    val filePicker = rememberLauncherForActivityResult(
        ActivityResultContracts.GetContent()
    ) { uri: Uri? ->
        if (uri == null) return@rememberLauncherForActivityResult
        importing = true
        scope.launch {
            val name = uri.lastPathSegment ?: "import.mbtiles"
            val result = withContext(Dispatchers.IO) {
                MbTilesManager.importFromUri(context, uri, name)
            }
            importing = false
            statusMsg = if (result != null) "Imported ${result.name}" else "Import failed"
            refresh()
        }
    }

    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text("Offline Maps") },
                navigationIcon = {
                    IconButton(onClick = { navController.popBackStack() }) {
                        Icon(Icons.AutoMirrored.Filled.ArrowBack, "Back")
                    }
                }
            )
        }
    ) { padding ->
        Column(
            modifier = Modifier
                .padding(padding)
                .fillMaxSize()
                .padding(16.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            if (statusMsg != null) {
                Surface(
                    color = MaterialTheme.colorScheme.primaryContainer,
                    shape = MaterialTheme.shapes.medium,
                    modifier = Modifier.fillMaxWidth()
                ) {
                    Text(
                        statusMsg!!,
                        modifier = Modifier.padding(12.dp),
                        style = MaterialTheme.typography.bodySmall
                    )
                }
            }

            Text("Installed Tile Archives", style = MaterialTheme.typography.titleMedium)

            if (archives.isEmpty()) {
                Text(
                    "No offline tile archives installed. Import an .mbtiles file or download a region.",
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
            } else {
                LazyColumn(
                    modifier = Modifier.weight(1f),
                    verticalArrangement = Arrangement.spacedBy(8.dp)
                ) {
                    items(archives, key = { it.absolutePath }) { file ->
                        ArchiveCard(
                            file = file,
                            onDelete = {
                                MbTilesManager.delete(file)
                                refresh()
                                statusMsg = "Deleted ${file.name}"
                            }
                        )
                    }
                }
            }

            Spacer(Modifier.weight(1f))

            if (importing) {
                LinearProgressIndicator(modifier = Modifier.fillMaxWidth())
                Text("Importing…", style = MaterialTheme.typography.bodySmall)
            }

            downloadProgress?.let { (done, total) ->
                LinearProgressIndicator(
                    progress = { if (total > 0) done.toFloat() / total else 0f },
                    modifier = Modifier.fillMaxWidth()
                )
                Text("Downloading tiles $done / $total", style = MaterialTheme.typography.bodySmall)
            }

            Row(
                horizontalArrangement = Arrangement.spacedBy(8.dp),
                modifier = Modifier.fillMaxWidth()
            ) {
                OutlinedButton(
                    onClick = { filePicker.launch("*/*") },
                    modifier = Modifier.weight(1f),
                    enabled = !importing && downloadProgress == null
                ) {
                    Text("Import .mbtiles")
                }
                Button(
                    onClick = { showDownloadDialog = true },
                    modifier = Modifier.weight(1f),
                    enabled = !importing && downloadProgress == null
                ) {
                    Icon(Icons.Default.Download, null)
                    Spacer(Modifier.width(4.dp))
                    Text("Download Region")
                }
            }

            Text(
                "Imported archives are automatically loaded by the map when offline. " +
                "Both vector (MBTiles/PBF) and raster (MBTiles/PNG) formats are supported.",
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
        }
    }

    if (showDownloadDialog) {
        RegionDownloadDialog(
            onConfirm = { north, south, east, west, minZ, maxZ ->
                showDownloadDialog = false
                downloadProgress = 0 to 0
                scope.launch {
                    withContext(Dispatchers.IO) {
                        MbTilesManager.downloadRegion(
                            context,
                            urlTemplate = "https://tile.openstreetmap.org/{z}/{x}/{y}.png",
                            minZoom = minZ,
                            maxZoom = maxZ,
                            north = north, south = south, east = east, west = west,
                            onProgress = { done, total ->
                                downloadProgress = done to total
                            }
                        )
                    }
                    downloadProgress = null
                    refresh()
                    statusMsg = "Download complete"
                }
            },
            onDismiss = { showDownloadDialog = false }
        )
    }
}

@Composable
private fun ArchiveCard(file: File, onDelete: () -> Unit) {
    Card(modifier = Modifier.fillMaxWidth()) {
        Row(
            modifier = Modifier.padding(12.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            Column(modifier = Modifier.weight(1f)) {
                Text(file.name, style = MaterialTheme.typography.titleSmall)
                Text(
                    "${file.length() / 1024} kB",
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
            }
            IconButton(onClick = onDelete) {
                Icon(Icons.Default.Delete, "Delete", tint = MaterialTheme.colorScheme.error)
            }
        }
    }
}

@Composable
private fun RegionDownloadDialog(
    onConfirm: (north: Double, south: Double, east: Double, west: Double, minZ: Int, maxZ: Int) -> Unit,
    onDismiss: () -> Unit
) {
    var north by remember { mutableStateOf("32.2") }
    var south by remember { mutableStateOf("31.5") }
    var east  by remember { mutableStateOf("35.3") }
    var west  by remember { mutableStateOf("34.7") }
    var minZ  by remember { mutableStateOf("10") }
    var maxZ  by remember { mutableStateOf("13") }
    var error by remember { mutableStateOf<String?>(null) }

    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text("Download Tile Region") },
        text = {
            Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                Text(
                    "Coordinates in decimal degrees. Keep zoom range small to avoid " +
                    "downloading thousands of tiles.",
                    style = MaterialTheme.typography.bodySmall
                )
                Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    OutlinedTextField(north, { north = it }, label = { Text("North") }, modifier = Modifier.weight(1f), singleLine = true)
                    OutlinedTextField(south, { south = it }, label = { Text("South") }, modifier = Modifier.weight(1f), singleLine = true)
                }
                Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    OutlinedTextField(east,  { east  = it }, label = { Text("East")  }, modifier = Modifier.weight(1f), singleLine = true)
                    OutlinedTextField(west,  { west  = it }, label = { Text("West")  }, modifier = Modifier.weight(1f), singleLine = true)
                }
                Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    OutlinedTextField(minZ, { minZ = it }, label = { Text("Min zoom") }, modifier = Modifier.weight(1f), singleLine = true)
                    OutlinedTextField(maxZ, { maxZ = it }, label = { Text("Max zoom") }, modifier = Modifier.weight(1f), singleLine = true)
                }
                if (error != null) Text(error!!, color = MaterialTheme.colorScheme.error, style = MaterialTheme.typography.bodySmall)
            }
        },
        confirmButton = {
            TextButton(onClick = {
                val n = north.toDoubleOrNull(); val s = south.toDoubleOrNull()
                val e = east.toDoubleOrNull();  val w = west.toDoubleOrNull()
                val minZi = minZ.toIntOrNull(); val maxZi = maxZ.toIntOrNull()
                if (n == null || s == null || e == null || w == null || minZi == null || maxZi == null) {
                    error = "All fields must be valid numbers"
                } else if (maxZi - minZi > 4) {
                    error = "Zoom range too large (max 4 levels). Reduce to limit tile count."
                } else {
                    onConfirm(n, s, e, w, minZi, maxZi)
                }
            }) { Text("Download") }
        },
        dismissButton = { TextButton(onClick = onDismiss) { Text("Cancel") } }
    )
}
