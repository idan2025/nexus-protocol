package com.nexus.mesh.call

import android.media.AudioFormat
import android.media.AudioManager
import android.media.AudioRecord
import android.media.AudioTrack
import android.media.MediaRecorder
import android.util.Log
import com.nexus.mesh.nxm.VoiceCodec
import kotlinx.coroutines.*
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow

/**
 * Manages real-time PTT audio capture and playback over raw PCM.
 *
 * Capture path: AudioRecord → 20ms PCM frames → [onAudioFrame] callback
 * Playback path: [playAudioChunk] → AudioTrack stream
 *
 * Uses PCM_8K (raw 16-bit signed mono 8 kHz) — no native Opus encoder
 * required, works on all API levels, and fits within the NXM 3824B ceiling
 * (20ms frame = 320 bytes, well under the 2800B chunk limit).
 */
class PttMediaSessionManager {

    companion object {
        private const val TAG = "PttMediaSession"
        const val SAMPLE_RATE = 8000
        const val FRAME_BYTES = 320        // 20ms at 8kHz × 16-bit mono
        const val CODEC = VoiceCodec.PCM_8K
    }

    private val scope = CoroutineScope(Dispatchers.IO + SupervisorJob())

    private val _isMicActive = MutableStateFlow(false)
    val isMicActive: StateFlow<Boolean> = _isMicActive

    /** Called from the capture coroutine with each 20ms PCM frame. */
    var onAudioFrame: ((ByteArray) -> Unit)? = null

    private var audioRecord: AudioRecord? = null
    private var captureJob: Job? = null

    private var audioTrack: AudioTrack? = null

    fun startCapture() {
        if (_isMicActive.value) return
        val minBuf = AudioRecord.getMinBufferSize(
            SAMPLE_RATE, AudioFormat.CHANNEL_IN_MONO, AudioFormat.ENCODING_PCM_16BIT
        )
        val bufSize = maxOf(minBuf, FRAME_BYTES * 4)
        val rec = AudioRecord(
            MediaRecorder.AudioSource.VOICE_COMMUNICATION,
            SAMPLE_RATE,
            AudioFormat.CHANNEL_IN_MONO,
            AudioFormat.ENCODING_PCM_16BIT,
            bufSize
        )
        if (rec.state != AudioRecord.STATE_INITIALIZED) {
            rec.release()
            Log.e(TAG, "AudioRecord init failed")
            return
        }
        audioRecord = rec
        rec.startRecording()
        _isMicActive.value = true
        captureJob = scope.launch {
            val frame = ByteArray(FRAME_BYTES)
            while (isActive) {
                val read = rec.read(frame, 0, frame.size)
                if (read > 0) onAudioFrame?.invoke(frame.copyOf(read))
            }
        }
    }

    fun stopCapture() {
        captureJob?.cancel()
        captureJob = null
        audioRecord?.apply { stop(); release() }
        audioRecord = null
        _isMicActive.value = false
    }

    fun playAudioChunk(data: ByteArray) {
        if (audioTrack == null) ensurePlayback()
        audioTrack?.write(data, 0, data.size)
    }

    fun ensurePlayback() {
        if (audioTrack != null) return
        startPlayback()
    }

    private fun startPlayback() {
        val minBuf = AudioTrack.getMinBufferSize(
            SAMPLE_RATE, AudioFormat.CHANNEL_OUT_MONO, AudioFormat.ENCODING_PCM_16BIT
        )
        val track = AudioTrack(
            AudioManager.STREAM_VOICE_CALL,
            SAMPLE_RATE,
            AudioFormat.CHANNEL_OUT_MONO,
            AudioFormat.ENCODING_PCM_16BIT,
            maxOf(minBuf, FRAME_BYTES * 4),
            AudioTrack.MODE_STREAM
        )
        track.play()
        audioTrack = track
    }

    fun release() {
        stopCapture()
        audioTrack?.apply { stop(); release() }
        audioTrack = null
        scope.cancel()
    }
}
