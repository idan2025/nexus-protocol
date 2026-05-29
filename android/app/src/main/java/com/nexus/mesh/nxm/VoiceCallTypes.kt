package com.nexus.mesh.nxm

object VoiceCallState {
    const val INVITE   = 0x01  // Caller → Callee: propose call
    const val ACCEPT   = 0x02  // Callee → Caller: accepted, ready for audio
    const val REJECT   = 0x03  // Callee → Caller: rejected
    const val HANGUP   = 0x04  // Either party: terminate call
    const val AUDIO    = 0x05  // Either party: real-time audio chunk
    const val PTT_START = 0x06 // Sender beginning PTT transmission
    const val PTT_END   = 0x07 // Sender finished PTT transmission
}

object VoiceCodec {
    const val PCM_8K   = 0x01  // Raw PCM 16-bit signed mono 8 kHz
    const val OPUS_8K  = 0x02  // Opus 8 kbps (narrowband voice)
    const val OPUS_16K = 0x03  // Opus 16 kbps (wideband voice)
}
