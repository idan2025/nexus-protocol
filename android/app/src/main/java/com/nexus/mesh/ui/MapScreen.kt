package com.nexus.mesh.ui

import androidx.compose.foundation.layout.*
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.filled.Map
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.viewinterop.AndroidView
import androidx.navigation.NavController
import com.nexus.mesh.maps.MbTilesManager
import org.osmdroid.config.Configuration
import org.osmdroid.tileprovider.MapTileProviderArray
import org.osmdroid.tileprovider.modules.ArchiveFileFactory
import org.osmdroid.tileprovider.modules.MapTileFileArchiveProvider
import org.osmdroid.tileprovider.modules.MapTileModuleProviderBase
import org.osmdroid.tileprovider.tilesource.TileSourceFactory
import org.osmdroid.tileprovider.util.SimpleRegisterReceiver
import org.osmdroid.util.GeoPoint
import org.osmdroid.views.MapView
import org.osmdroid.views.overlay.Marker

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun MapScreen(
    navController: NavController,
    lat: Double,
    lon: Double
) {
    val context = LocalContext.current

    LaunchedEffect(Unit) {
        Configuration.getInstance().userAgentValue = "NexusMesh/0.1"
    }

    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text("%.5f, %.5f".format(lat, lon)) },
                navigationIcon = {
                    IconButton(onClick = { navController.popBackStack() }) {
                        Icon(Icons.AutoMirrored.Filled.ArrowBack, "Back")
                    }
                },
                actions = {
                    IconButton(onClick = { navController.navigate("offline_maps") }) {
                        Icon(Icons.Default.Map, contentDescription = "Offline Maps")
                    }
                }
            )
        }
    ) { padding ->
        AndroidView(
            factory = { ctx ->
                MapView(ctx).apply {
                    val archives = MbTilesManager.listArchives(ctx)
                    if (archives.isNotEmpty()) {
                        // Load local MBTiles archives and fall back to online if needed
                        val tileSource = TileSourceFactory.MAPNIK
                        val archiveProviders = archives.mapNotNull { file ->
                            try {
                                val archive = ArchiveFileFactory.getArchiveFile(file)
                                if (archive != null) {
                                    MapTileFileArchiveProvider(
                                        SimpleRegisterReceiver(ctx),
                                        tileSource,
                                        arrayOf(archive)
                                    )
                                } else null
                            } catch (e: Exception) { null }
                        }
                        if (archiveProviders.isNotEmpty()) {
                            val providerArray = archiveProviders.toTypedArray<MapTileModuleProviderBase>()
                            tileProvider = MapTileProviderArray(tileSource, null, providerArray)
                        } else {
                            setTileSource(tileSource)
                        }
                    } else {
                        setTileSource(TileSourceFactory.MAPNIK)
                    }

                    setMultiTouchControls(true)
                    controller.setZoom(16.0)
                    controller.setCenter(GeoPoint(lat, lon))

                    val marker = Marker(this)
                    marker.position = GeoPoint(lat, lon)
                    marker.setAnchor(Marker.ANCHOR_CENTER, Marker.ANCHOR_BOTTOM)
                    marker.title = "Shared Location"
                    overlays.add(marker)
                }
            },
            modifier = Modifier
                .fillMaxSize()
                .padding(padding)
        )
    }
}
