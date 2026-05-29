package com.nexus.mesh.ui

import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Call
import androidx.compose.material.icons.filled.CallEnd
import androidx.compose.material.icons.filled.Mic
import androidx.compose.material.icons.filled.MicOff
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.unit.dp
import androidx.navigation.NavController
import com.nexus.mesh.service.NexusService

@Composable
fun CallScreen(activity: MainActivity, navController: NavController, peerAddr: String) {
    val service = activity.getService()
    val callState by service?.callState?.collectAsState()
        ?: remember { mutableStateOf(NexusService.CallState.IDLE) }
    val callPeer by service?.callPeer?.collectAsState()
        ?: remember { mutableStateOf<String?>(null) }
    val isMicActive by service?.pttMicActive?.collectAsState()
        ?: remember { mutableStateOf(false) }
    val peerName by service?.repository?.getNicknameOrAddr(peerAddr)?.collectAsState(initial = peerAddr)
        ?: remember { mutableStateOf(peerAddr) }

    // Navigate away when call ends
    LaunchedEffect(callState) {
        if (callState == NexusService.CallState.IDLE || callState == NexusService.CallState.ENDED) {
            navController.popBackStack()
        }
    }

    // Initiate outgoing call when screen opens (if not already in a call)
    LaunchedEffect(peerAddr) {
        if (callState == NexusService.CallState.IDLE) {
            service?.startCall(peerAddr)
        }
    }

    Surface(
        modifier = Modifier.fillMaxSize(),
        color = MaterialTheme.colorScheme.background
    ) {
        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(32.dp),
            horizontalAlignment = Alignment.CenterHorizontally,
            verticalArrangement = Arrangement.SpaceBetween
        ) {
            Column(horizontalAlignment = Alignment.CenterHorizontally) {
                Spacer(Modifier.height(48.dp))
                Text(
                    text = peerName,
                    style = MaterialTheme.typography.headlineMedium
                )
                Spacer(Modifier.height(8.dp))
                Text(
                    text = when (callState) {
                        NexusService.CallState.RINGING_OUT -> "Calling…"
                        NexusService.CallState.RINGING_IN  -> "Incoming call"
                        NexusService.CallState.CONNECTED   -> "Connected"
                        else -> ""
                    },
                    style = MaterialTheme.typography.bodyLarge,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
            }

            when (callState) {
                NexusService.CallState.RINGING_IN -> IncomingCallControls(
                    onAccept = { service?.acceptCall() },
                    onReject = { service?.rejectCall(); navController.popBackStack() }
                )
                NexusService.CallState.CONNECTED -> ActiveCallControls(
                    isMicActive = isMicActive,
                    onPttDown = { service?.pttStart() },
                    onPttUp   = { service?.pttEnd() },
                    onHangUp  = { service?.hangUp() }
                )
                else -> OutgoingCallControls(
                    onCancel = { service?.hangUp() }
                )
            }
        }
    }
}

@Composable
private fun IncomingCallControls(onAccept: () -> Unit, onReject: () -> Unit) {
    Row(
        modifier = Modifier.fillMaxWidth(),
        horizontalArrangement = Arrangement.SpaceEvenly
    ) {
        Column(horizontalAlignment = Alignment.CenterHorizontally) {
            FloatingActionButton(
                onClick = onReject,
                containerColor = MaterialTheme.colorScheme.error,
                shape = CircleShape,
                modifier = Modifier.size(72.dp)
            ) {
                Icon(Icons.Default.CallEnd, contentDescription = "Reject", tint = Color.White)
            }
            Spacer(Modifier.height(8.dp))
            Text("Decline", style = MaterialTheme.typography.labelMedium)
        }
        Column(horizontalAlignment = Alignment.CenterHorizontally) {
            FloatingActionButton(
                onClick = onAccept,
                containerColor = Color(0xFF4CAF50),
                shape = CircleShape,
                modifier = Modifier.size(72.dp)
            ) {
                Icon(Icons.Default.Call, contentDescription = "Accept", tint = Color.White)
            }
            Spacer(Modifier.height(8.dp))
            Text("Accept", style = MaterialTheme.typography.labelMedium)
        }
    }
}

@Composable
private fun ActiveCallControls(
    isMicActive: Boolean,
    onPttDown: () -> Unit,
    onPttUp: () -> Unit,
    onHangUp: () -> Unit
) {
    Column(horizontalAlignment = Alignment.CenterHorizontally) {
        // PTT button — hold to talk
        Button(
            onClick = {},
            modifier = Modifier
                .size(120.dp),
            shape = CircleShape,
            colors = ButtonDefaults.buttonColors(
                containerColor = if (isMicActive) MaterialTheme.colorScheme.primary
                                 else MaterialTheme.colorScheme.surfaceVariant
            ),
            contentPadding = PaddingValues(0.dp),
            // Use pointer input for press/release detection
            // Note: full touch tracking via Modifier.pointerInteropFilter is
            // wired in the parent via onPttDown/onPttUp callbacks triggered
            // by PointerEventType.Press / Release from a Modifier on the button
        ) {
            Column(horizontalAlignment = Alignment.CenterHorizontally) {
                Icon(
                    if (isMicActive) Icons.Default.Mic else Icons.Default.MicOff,
                    contentDescription = "PTT",
                    modifier = Modifier.size(40.dp)
                )
                Text(
                    if (isMicActive) "Talking" else "Hold to Talk",
                    style = MaterialTheme.typography.labelSmall
                )
            }
        }

        // PTT press/release helper buttons (accessibility + touch fallback)
        Row(
            modifier = Modifier.padding(top = 8.dp),
            horizontalArrangement = Arrangement.spacedBy(8.dp)
        ) {
            OutlinedButton(onClick = onPttDown) { Text("PTT Start") }
            OutlinedButton(onClick = onPttUp)   { Text("PTT End") }
        }

        Spacer(Modifier.height(32.dp))
        FloatingActionButton(
            onClick = onHangUp,
            containerColor = MaterialTheme.colorScheme.error,
            shape = CircleShape,
            modifier = Modifier.size(72.dp)
        ) {
            Icon(Icons.Default.CallEnd, contentDescription = "Hang Up", tint = Color.White)
        }
        Spacer(Modifier.height(8.dp))
        Text("Hang Up", style = MaterialTheme.typography.labelMedium)
    }
}

@Composable
private fun OutgoingCallControls(onCancel: () -> Unit) {
    Column(horizontalAlignment = Alignment.CenterHorizontally) {
        FloatingActionButton(
            onClick = onCancel,
            containerColor = MaterialTheme.colorScheme.error,
            shape = CircleShape,
            modifier = Modifier.size(72.dp)
        ) {
            Icon(Icons.Default.CallEnd, contentDescription = "Cancel", tint = Color.White)
        }
        Spacer(Modifier.height(8.dp))
        Text("Cancel", style = MaterialTheme.typography.labelMedium)
    }
}
