package com.nexus.mesh.ui

import android.graphics.Bitmap
import androidx.compose.foundation.Image
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
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
import com.nexus.mesh.data.PaperMessage

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun PaperMessageScreen(activity: MainActivity, navController: NavController) {
    val service = activity.getService()
    val myAddress by service?.address?.collectAsState()
        ?: remember { mutableStateOf("--------") }

    var selectedTab by remember { mutableIntStateOf(0) }

    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text("Paper messages") },
                navigationIcon = {
                    IconButton(onClick = { navController.popBackStack() }) {
                        Icon(Icons.AutoMirrored.Filled.ArrowBack, "Back")
                    }
                }
            )
        }
    ) { padding ->
        Column(Modifier.fillMaxSize().padding(padding)) {
            TabRow(selectedTabIndex = selectedTab) {
                Tab(selected = selectedTab == 0, onClick = { selectedTab = 0 },
                    text = { Text("Compose") })
                Tab(selected = selectedTab == 1, onClick = { selectedTab = 1 },
                    text = { Text("Scan") })
            }
            when (selectedTab) {
                0 -> ComposeTab(myAddress)
                1 -> ImportTab(activity)
            }
        }
    }
}

@Composable
private fun ComposeTab(myAddress: String) {
    var text by remember { mutableStateOf("") }
    val envelope = remember(text, myAddress) {
        if (text.isBlank() || myAddress.length != 8) null
        else PaperMessage.Envelope(
            fromAddr = myAddress,
            timestampMs = System.currentTimeMillis(),
            text = text
        )
    }
    val uri = remember(envelope) { envelope?.let { PaperMessage.encode(it) } }

    Column(
        modifier = Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(16.dp),
        horizontalAlignment = Alignment.CenterHorizontally
    ) {
        OutlinedTextField(
            value = text,
            onValueChange = { text = it.take(1800) },
            label = { Text("Message (up to 1800 chars)") },
            modifier = Modifier.fillMaxWidth(),
            minLines = 3,
            maxLines = 8
        )
        Spacer(Modifier.height(12.dp))
        Text(
            "From: $myAddress",
            style = MaterialTheme.typography.labelMedium,
            color = MaterialTheme.colorScheme.onSurfaceVariant
        )
        Spacer(Modifier.height(12.dp))

        if (uri != null) {
            val bmp = remember(uri) { qrBitmap(uri) }
            if (bmp != null) {
                Card {
                    Image(
                        bitmap = bmp.asImageBitmap(),
                        contentDescription = "Paper message QR",
                        modifier = Modifier.size(300.dp).padding(16.dp)
                    )
                }
            } else {
                Text("Message too large for a single QR code.",
                    color = MaterialTheme.colorScheme.error)
            }
            Spacer(Modifier.height(12.dp))
            Text(
                "Anyone can scan this QR to deliver the message when they're next online.",
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
        } else {
            Text("Type a message to generate a QR code.",
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant)
        }
    }
}

@Composable
private fun ImportTab(activity: MainActivity) {
    val context = LocalContext.current
    Column(
        modifier = Modifier.fillMaxSize().padding(24.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.Center
    ) {
        Button(onClick = {
            val opts = com.journeyapps.barcodescanner.ScanOptions()
                .setDesiredBarcodeFormats(com.journeyapps.barcodescanner.ScanOptions.QR_CODE)
                .setPrompt("Scan a paper message QR")
                .setBeepEnabled(false)
                .setOrientationLocked(true)
            if (context is MainActivity) {
                context.qrScanLauncher?.launch(opts)
            }
        }) {
            Text("Open scanner")
        }
        Spacer(Modifier.height(16.dp))
        Text(
            "Scanned paper messages are delivered into the matching conversation " +
                "as incoming traffic from the sender's address.",
            style = MaterialTheme.typography.bodyMedium,
            color = MaterialTheme.colorScheme.onSurfaceVariant
        )
    }
}

private fun qrBitmap(content: String, size: Int = 640): Bitmap? {
    return try {
        val matrix = QRCodeWriter().encode(content, BarcodeFormat.QR_CODE, size, size)
        val bmp = Bitmap.createBitmap(size, size, Bitmap.Config.RGB_565)
        for (x in 0 until size) {
            for (y in 0 until size) {
                bmp.setPixel(x, y, if (matrix[x, y]) 0xFF000000.toInt() else 0xFFFFFFFF.toInt())
            }
        }
        bmp
    } catch (_: Exception) {
        null
    }
}
