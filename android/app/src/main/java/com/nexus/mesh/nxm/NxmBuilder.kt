package com.nexus.mesh.nxm

import java.nio.ByteBuffer
import java.nio.ByteOrder

object NxmBuilder {

    fun buildText(text: String, msgId: ByteArray? = null,
                  replyTo: ByteArray? = null,
                  title: String? = null): ByteArray {
        var flags = 0
        val fields = mutableListOf<Pair<NxmFieldType, ByteArray>>()

        val id = msgId ?: generateMsgId()
        fields.add(NxmFieldType.MSG_ID to id)
        if (title != null) {
            val tb = title.toByteArray(Charsets.UTF_8)
            fields.add(NxmFieldType.TITLE to (if (tb.size > 128) tb.copyOf(128) else tb))
        }
        fields.add(NxmFieldType.TEXT to text.toByteArray(Charsets.UTF_8))

        if (replyTo != null) {
            flags = flags or NxmFlag.REPLY
            fields.add(NxmFieldType.REPLY_TO to replyTo)
        }

        return serialize(NxmType.TEXT, flags, fields)
    }

    fun buildAck(targetMsgId: ByteArray): ByteArray {
        return serialize(NxmType.ACK, 0,
            listOf(NxmFieldType.MSG_ID to targetMsgId))
    }

    fun buildRead(targetMsgId: ByteArray): ByteArray {
        return serialize(NxmType.READ, 0,
            listOf(NxmFieldType.MSG_ID to targetMsgId))
    }

    fun buildLocation(lat: Double, lon: Double,
                      altM: Int = 0, accuracyM: Int = 0): ByteArray {
        val fields = mutableListOf<Pair<NxmFieldType, ByteArray>>()
        fields.add(NxmFieldType.MSG_ID to generateMsgId())

        val ilat = (lat * 1e7).toInt()
        val ilon = (lon * 1e7).toInt()
        fields.add(NxmFieldType.LATITUDE to
            ByteBuffer.allocate(4).order(ByteOrder.LITTLE_ENDIAN).putInt(ilat).array())
        fields.add(NxmFieldType.LONGITUDE to
            ByteBuffer.allocate(4).order(ByteOrder.LITTLE_ENDIAN).putInt(ilon).array())
        fields.add(NxmFieldType.ALTITUDE to
            ByteBuffer.allocate(2).order(ByteOrder.LITTLE_ENDIAN).putShort(altM.toShort()).array())
        fields.add(NxmFieldType.ACCURACY to byteArrayOf((accuracyM and 0xFF).toByte()))

        return serialize(NxmType.LOCATION, 0, fields)
    }

    /* Legacy single-message media builders (buildImage / buildFile /
     * buildVoice) were removed -- everything now goes through buildChunk()
     * so the sender's MSG_ID matches the receiver's reassembled message
     * id for ACK / delivery-tick tracking. */

    /** Encode a u16 as little-endian 2-byte array (PART_IDX / PART_TOTAL). */
    private fun u16le(v: Int): ByteArray = byteArrayOf(
        (v and 0xFF).toByte(),
        ((v shr 8) and 0xFF).toByte()
    )

    /**
     * One chunk of a multi-part media transfer. All chunks of the same
     * transfer share [msgId]; the first chunk (partIdx == 0) additionally
     * carries filename/mimetype/thumbnail/duration so the receiver can
     * construct the message envelope without waiting for all parts.
     *
     * type must be IMAGE, FILE, or VOICE_NOTE.
     *
     * Caller slices the source into chunks small enough to fit the
     * NX_FRAG_MAX_MESSAGE (3824B) ceiling after the envelope. In practice
     * keep chunk size ≤ 2800 bytes for headroom.
     */
    fun buildChunk(type: NxmType,
                   msgId: ByteArray,
                   partIdx: Int,
                   partTotal: Int,
                   chunk: ByteArray,
                   firstFilename: String? = null,
                   firstMime: String? = null,
                   firstThumbnail: ByteArray? = null,
                   firstDurationSec: Int? = null,
                   firstCodec: Int? = null): ByteArray {
        val fields = mutableListOf<Pair<NxmFieldType, ByteArray>>()
        fields.add(NxmFieldType.MSG_ID to msgId)
        fields.add(NxmFieldType.PART_IDX to u16le(partIdx))
        fields.add(NxmFieldType.PART_TOTAL to u16le(partTotal))
        if (partIdx == 0) {
            firstFilename?.let { fields.add(NxmFieldType.FILENAME to it.toByteArray(Charsets.UTF_8)) }
            firstMime?.let { fields.add(NxmFieldType.MIMETYPE to it.toByteArray(Charsets.UTF_8)) }
            firstThumbnail?.let { fields.add(NxmFieldType.THUMBNAIL to it) }
            firstDurationSec?.let { fields.add(NxmFieldType.DURATION to u16le(it)) }
            firstCodec?.let { fields.add(NxmFieldType.CODEC to byteArrayOf((it and 0xFF).toByte())) }
        }
        fields.add(NxmFieldType.FILEDATA to chunk)
        return serialize(type, 0, fields)
    }

    fun buildReaction(emoji: String, targetMsgId: ByteArray): ByteArray {
        return serialize(NxmType.REACTION, 0, listOf(
            NxmFieldType.MSG_ID to generateMsgId(),
            NxmFieldType.REACTION to emoji.toByteArray(Charsets.UTF_8),
            NxmFieldType.REPLY_TO to targetMsgId
        ))
    }

    fun buildTyping(): ByteArray {
        return serialize(NxmType.TYPING, 0, emptyList())
    }

    /**
     * Build a VOICE_CALL signaling frame (INVITE / ACCEPT / REJECT / HANGUP /
     * PTT_START / PTT_END).  [sessionId] is a 4-byte shared call ID generated
     * by the caller.  [codec] is only required on INVITE so the peer knows
     * which codec will be used for audio.
     */
    fun buildVoiceCallSignal(
        sessionId: ByteArray,
        callState: Int,
        codec: Int? = null
    ): ByteArray {
        val fields = mutableListOf<Pair<NxmFieldType, ByteArray>>()
        fields.add(NxmFieldType.MSG_ID to sessionId)
        fields.add(NxmFieldType.CALL_STATE to byteArrayOf(callState.toByte()))
        codec?.let { fields.add(NxmFieldType.CODEC to byteArrayOf(it.toByte())) }
        return serialize(NxmType.VOICE_CALL, NxmFlag.URGENT, fields)
    }

    /**
     * Build a VOICE_CALL audio frame carrying one PCM/Opus chunk.
     * Marked URGENT so the mesh does not queue it behind bulk traffic.
     */
    fun buildVoiceAudio(sessionId: ByteArray, audio: ByteArray, codec: Int): ByteArray {
        return serialize(
            NxmType.VOICE_CALL,
            NxmFlag.URGENT,
            listOf(
                NxmFieldType.MSG_ID    to sessionId,
                NxmFieldType.CALL_STATE to byteArrayOf(VoiceCallState.AUDIO.toByte()),
                NxmFieldType.CODEC      to byteArrayOf(codec.toByte()),
                NxmFieldType.FILEDATA   to audio
            )
        )
    }

    fun buildNickname(name: String): ByteArray {
        return serialize(NxmType.NICKNAME, 0,
            listOf(NxmFieldType.NICKNAME to name.toByteArray(Charsets.UTF_8).take(32).toByteArray()))
    }

    fun generateMsgId(): ByteArray {
        val ts = (System.currentTimeMillis() / 1000).toInt()
        val rnd = ByteArray(2).also { java.security.SecureRandom().nextBytes(it) }
        return byteArrayOf(
            (ts and 0xFF).toByte(),
            ((ts shr 8) and 0xFF).toByte(),
            rnd[0], rnd[1]
        )
    }

    private fun serialize(type: NxmType, flags: Int,
                          fields: List<Pair<NxmFieldType, ByteArray>>): ByteArray {
        val ts = (System.currentTimeMillis() / 1000).toInt()

        // Calculate total size
        var totalSize = NXM_HEADER_SIZE
        for ((_, data) in fields) {
            totalSize += NXM_FIELD_HEADER + data.size
        }

        val buf = ByteBuffer.allocate(totalSize).order(ByteOrder.LITTLE_ENDIAN)
        buf.put(NXM_VERSION.toByte())
        buf.put(type.value.toByte())
        buf.put(flags.toByte())
        buf.putInt(ts)
        buf.put(fields.size.toByte())

        for ((ft, data) in fields) {
            buf.put(ft.value.toByte())
            buf.putShort(data.size.toShort())
            buf.put(data)
        }

        return buf.array()
    }
}
