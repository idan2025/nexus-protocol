package com.nexus.mesh.ui

import android.graphics.Bitmap
import androidx.compose.foundation.Image
import androidx.compose.foundation.layout.*
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import androidx.navigation.NavController
import com.google.zxing.BarcodeFormat
import com.google.zxing.qrcode.QRCodeWriter

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun QrScreen(activity: MainActivity, navController: NavController) {
    val service = activity.getService()
    val address by service?.address?.collectAsState()
        ?: remember { mutableStateOf("--------") }

    var selectedTab by remember { mutableIntStateOf(0) }

    val signPubkeyHex = remember(address) {
        val node = service?.let { svc ->
            // Access node via reflection-free approach: get pubkey through JNI
            try {
                val field = svc.javaClass.getDeclaredField("node")
                field.isAccessible = true
                val n = field.get(svc) as? com.nexus.mesh.service.NexusNode
                n?.getSignPubkey()?.joinToString("") { "%02X".format(it) }
            } catch (e: Exception) {
                null
            }
        }
        node ?: ""
    }

    val qrContent = "nexus://$address/$signPubkeyHex"

    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text("QR Code") },
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
                .fillMaxSize()
                .padding(padding)
        ) {
            TabRow(selectedTabIndex = selectedTab) {
                Tab(
                    selected = selectedTab == 0,
                    onClick = { selectedTab = 0 },
                    text = { Text("My QR") }
                )
                Tab(
                    selected = selectedTab == 1,
                    onClick = { selectedTab = 1 },
                    text = { Text("Scan") }
                )
            }

            when (selectedTab) {
                0 -> MyQrTab(qrContent, address)
                1 -> ScanTab(navController)
            }
        }
    }
}

@Composable
private fun MyQrTab(qrContent: String, address: String) {
    val qrBitmap = remember(qrContent) {
        try {
            val writer = QRCodeWriter()
            val matrix = writer.encode(qrContent, BarcodeFormat.QR_CODE, 512, 512)
            val bmp = Bitmap.createBitmap(512, 512, Bitmap.Config.RGB_565)
            for (x in 0 until 512) {
                for (y in 0 until 512) {
                    bmp.setPixel(x, y, if (matrix[x, y]) 0xFF000000.toInt() else 0xFFFFFFFF.toInt())
                }
            }
            bmp
        } catch (e: Exception) {
            null
        }
    }

    Column(
        modifier = Modifier
            .fillMaxSize()
            .padding(24.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.Center
    ) {
        Text(
            "Your NEXUS Address",
            style = MaterialTheme.typography.titleMedium
        )
        Spacer(Modifier.height(8.dp))
        Text(
            address,
            style = MaterialTheme.typography.titleLarge,
            color = MaterialTheme.colorScheme.primary
        )
        Spacer(Modifier.height(24.dp))

        if (qrBitmap != null) {
            Card {
                Image(
                    bitmap = qrBitmap.asImageBitmap(),
                    contentDescription = "QR Code",
                    modifier = Modifier
                        .size(280.dp)
                        .padding(16.dp)
                )
            }
        } else {
            Text("Could not generate QR code")
        }

        Spacer(Modifier.height(16.dp))
        Text(
            "Share this QR code to let others add you",
            style = MaterialTheme.typography.bodyMedium,
            color = MaterialTheme.colorScheme.onSurfaceVariant
        )
    }
}

@Composable
private fun ScanTab(navController: NavController) {
    val context = LocalContext.current
    var scannedResult by remember { mutableStateOf<String?>(null) }

    Column(
        modifier = Modifier
            .fillMaxSize()
            .padding(24.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.Center
    ) {
        Button(
            onClick = {
                val integrator = com.journeyapps.barcodescanner.ScanOptions()
                    .setDesiredBarcodeFormats(com.journeyapps.barcodescanner.ScanOptions.QR_CODE)
                    .setPrompt("Scan a NEXUS QR code")
                    .setBeepEnabled(false)
                    .setOrientationLocked(true)

                // Launch scanner via activity
                if (context is MainActivity) {
                    context.qrScanLauncher?.launch(integrator)
                }
            }
        ) {
            Text("Open Scanner")
        }

        Spacer(Modifier.height(16.dp))

        Text(
            "Scan another node's QR code to start a conversation",
            style = MaterialTheme.typography.bodyMedium,
            color = MaterialTheme.colorScheme.onSurfaceVariant
        )

        if (scannedResult != null) {
            Spacer(Modifier.height(16.dp))
            Card(modifier = Modifier.fillMaxWidth()) {
                Column(modifier = Modifier.padding(16.dp)) {
                    Text("Scanned:", style = MaterialTheme.typography.labelMedium)
                    Text(scannedResult!!, style = MaterialTheme.typography.bodyMedium)
                }
            }
        }
    }
}
