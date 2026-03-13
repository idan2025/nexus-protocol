package com.nexus.mesh.ui

import android.graphics.BitmapFactory
import androidx.compose.foundation.Image
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.AttachFile
import androidx.compose.material.icons.filled.PlayArrow
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.unit.dp
import com.nexus.mesh.data.MessageEntity
import com.nexus.mesh.data.MessageType

@Composable
fun MediaBubble(
    msg: MessageEntity,
    isOutgoing: Boolean,
    onImageClick: (() -> Unit)? = null
) {
    val bubbleColor = if (isOutgoing)
        MaterialTheme.colorScheme.primary
    else
        MaterialTheme.colorScheme.surfaceVariant
    val textColor = if (isOutgoing)
        MaterialTheme.colorScheme.onPrimary
    else
        MaterialTheme.colorScheme.onSurfaceVariant

    Surface(
        shape = MaterialTheme.shapes.medium,
        color = bubbleColor,
        modifier = Modifier.widthIn(max = 250.dp)
    ) {
        Column(modifier = Modifier.padding(8.dp)) {
            when (msg.messageType) {
                MessageType.IMAGE -> {
                    // Try to load image from mediaPath
                    val bitmap = remember(msg.mediaPath) {
                        msg.mediaPath?.let {
                            try { BitmapFactory.decodeFile(it) } catch (e: Exception) { null }
                        }
                    }
                    if (bitmap != null) {
                        Image(
                            bitmap = bitmap.asImageBitmap(),
                            contentDescription = msg.fileName ?: "Image",
                            modifier = Modifier
                                .fillMaxWidth()
                                .heightIn(max = 200.dp)
                                .clickable { onImageClick?.invoke() },
                            contentScale = ContentScale.Fit
                        )
                    } else {
                        Row(verticalAlignment = Alignment.CenterVertically) {
                            Icon(Icons.Default.AttachFile, null, tint = textColor)
                            Spacer(Modifier.width(4.dp))
                            Text(msg.fileName ?: "Image", color = textColor)
                        }
                    }
                }
                MessageType.FILE -> {
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Icon(Icons.Default.AttachFile, null, tint = textColor,
                            modifier = Modifier.size(24.dp))
                        Spacer(Modifier.width(8.dp))
                        Column {
                            Text(
                                msg.fileName ?: "File",
                                color = textColor,
                                style = MaterialTheme.typography.bodyMedium
                            )
                            if (msg.mimeType != null) {
                                Text(
                                    msg.mimeType,
                                    color = textColor.copy(alpha = 0.7f),
                                    style = MaterialTheme.typography.labelSmall
                                )
                            }
                        }
                    }
                }
                MessageType.VOICE_NOTE -> {
                    Row(
                        verticalAlignment = Alignment.CenterVertically,
                        modifier = Modifier.fillMaxWidth()
                    ) {
                        IconButton(onClick = { /* TODO: playback */ }) {
                            Icon(Icons.Default.PlayArrow, "Play", tint = textColor)
                        }
                        Column {
                            Text(
                                "Voice Note",
                                color = textColor,
                                style = MaterialTheme.typography.bodyMedium
                            )
                            Text(
                                "${msg.duration}s",
                                color = textColor.copy(alpha = 0.7f),
                                style = MaterialTheme.typography.labelSmall
                            )
                        }
                    }
                }
            }
        }
    }
}
