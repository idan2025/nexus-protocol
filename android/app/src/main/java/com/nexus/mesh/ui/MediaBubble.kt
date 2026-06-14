package com.nexus.mesh.ui

import android.graphics.BitmapFactory
import android.media.AudioFormat
import android.media.AudioManager
import android.media.AudioTrack
import androidx.compose.foundation.Image
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.AttachFile
import androidx.compose.material.icons.filled.PlayArrow
import androidx.compose.material.icons.filled.Stop
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.unit.dp
import com.nexus.mesh.data.MessageEntity
import com.nexus.mesh.data.MessageType
import java.io.File
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

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
                    var isPlaying by remember { mutableStateOf(false) }
                    var progress by remember { mutableFloatStateOf(0f) }
                    val scope = rememberCoroutineScope()
                    val trackRef = remember { mutableStateOf<AudioTrack?>(null) }

                    DisposableEffect(msg.id) {
                        onDispose {
                            trackRef.value?.let { t -> t.stop(); t.release() }
                            trackRef.value = null
                        }
                    }

                    Row(
                        verticalAlignment = Alignment.CenterVertically,
                        modifier = Modifier.fillMaxWidth()
                    ) {
                        IconButton(onClick = {
                            if (isPlaying) {
                                trackRef.value?.let { t -> t.stop(); t.release() }
                                trackRef.value = null
                                isPlaying = false
                                progress = 0f
                            } else {
                                val path = msg.mediaPath ?: return@IconButton
                                scope.launch(Dispatchers.IO) {
                                    val pcm = try {
                                        File(path).readBytes()
                                    } catch (e: Exception) {
                                        return@launch
                                    }
                                    val sampleRate = 8000
                                    val minBuf = AudioTrack.getMinBufferSize(
                                        sampleRate,
                                        AudioFormat.CHANNEL_OUT_MONO,
                                        AudioFormat.ENCODING_PCM_16BIT
                                    )
                                    val track = AudioTrack(
                                        AudioManager.STREAM_MUSIC,
                                        sampleRate,
                                        AudioFormat.CHANNEL_OUT_MONO,
                                        AudioFormat.ENCODING_PCM_16BIT,
                                        maxOf(minBuf, pcm.size),
                                        AudioTrack.MODE_STATIC
                                    )
                                    track.write(pcm, 0, pcm.size)
                                    trackRef.value = track
                                    withContext(Dispatchers.Main) { isPlaying = true }
                                    track.play()
                                    // PCM_8K: 16-bit signed mono 8 kHz = 2 bytes per sample
                                    val totalMs = (pcm.size.toLong() * 1000L) / (sampleRate * 2)
                                    val startMs = System.currentTimeMillis()
                                    while (track.playState == AudioTrack.PLAYSTATE_PLAYING) {
                                        val elapsed = System.currentTimeMillis() - startMs
                                        withContext(Dispatchers.Main) {
                                            progress = (elapsed.toFloat() / totalMs).coerceIn(0f, 1f)
                                        }
                                        delay(50)
                                    }
                                    withContext(Dispatchers.Main) {
                                        isPlaying = false
                                        progress = 0f
                                    }
                                    trackRef.value = null
                                    track.release()
                                }
                            }
                        }) {
                            Icon(
                                if (isPlaying) Icons.Default.Stop else Icons.Default.PlayArrow,
                                if (isPlaying) "Stop" else "Play",
                                tint = textColor
                            )
                        }
                        Column(modifier = Modifier.weight(1f)) {
                            Text(
                                "Voice Note",
                                color = textColor,
                                style = MaterialTheme.typography.bodyMedium
                            )
                            if (isPlaying) {
                                LinearProgressIndicator(
                                    progress = { progress },
                                    modifier = Modifier
                                        .fillMaxWidth()
                                        .padding(top = 4.dp),
                                    color = textColor,
                                    trackColor = textColor.copy(alpha = 0.3f)
                                )
                            } else {
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
}
