package com.nexus.mesh.nxm

enum class NxmType(val value: Int) {
    TEXT(0x01),
    FILE(0x02),
    IMAGE(0x03),
    LOCATION(0x04),
    VOICE_NOTE(0x05),
    REACTION(0x06),
    ACK(0x07),
    TYPING(0x08),
    READ(0x09),
    DELETE(0x0A),
    NICKNAME(0x0B),
    CONTACT(0x0C),
    VOICE_CALL(0x0D);

    companion object {
        fun fromValue(v: Int): NxmType? = entries.find { it.value == v }
    }
}

enum class NxmFieldType(val value: Int) {
    TEXT(0x01),
    FILENAME(0x02),
    MIMETYPE(0x03),
    FILEDATA(0x04),
    LATITUDE(0x05),
    LONGITUDE(0x06),
    ALTITUDE(0x07),
    ACCURACY(0x08),
    REPLY_TO(0x09),
    REACTION(0x0A),
    MSG_ID(0x0B),
    NICKNAME(0x0C),
    DURATION(0x0D),
    THUMBNAIL(0x0E),
    CONTACT_ADDR(0x0F),
    CONTACT_PUB(0x10),
    CODEC(0x11),
    SIGNATURE(0x12),
    TITLE(0x13),
    STAMP(0x14),  // PoW stamp: [difficulty(1)][nonce(8 BE)]
    PART_IDX(0x15),    // u16 LE: 0-based index of this chunk in a multi-part transfer
    PART_TOTAL(0x16),  // u16 LE: total number of chunks (1 = single message)
    CALL_STATE(0x17);  // u8: VoiceCallState constant (INVITE/ACCEPT/REJECT/HANGUP/AUDIO)

    companion object {
        fun fromValue(v: Int): NxmFieldType? = entries.find { it.value == v }
    }
}

object NxmFlag {
    const val ENCRYPTED = 0x01
    const val SIGNED = 0x02
    const val PROPAGATE = 0x04
    const val URGENT = 0x08
    const val REPLY = 0x10
    const val GROUP = 0x20
    const val STAMPED = 0x40  // Message carries a proof-of-work stamp
}

const val NXM_VERSION = 1
const val NXM_HEADER_SIZE = 8
const val NXM_FIELD_HEADER = 3
const val NXM_MAX_FIELDS = 16

data class NxmField(val type: NxmFieldType, val data: ByteArray) {
    override fun equals(other: Any?): Boolean {
        if (this === other) return true
        if (other !is NxmField) return false
        return type == other.type && data.contentEquals(other.data)
    }
    override fun hashCode(): Int = 31 * type.hashCode() + data.contentHashCode()
}

data class NxmMessage(
    val version: Int,
    val type: NxmType,
    val flags: Int,
    val timestamp: Long,
    val fields: List<NxmField>
) {
    fun findField(type: NxmFieldType): NxmField? = fields.find { it.type == type }

    val text: String?
        get() = findField(NxmFieldType.TEXT)?.data?.toString(Charsets.UTF_8)

    val msgId: ByteArray?
        get() = findField(NxmFieldType.MSG_ID)?.data

    val msgIdHex: String?
        get() = msgId?.joinToString("") { "%02X".format(it) }

    val nickname: String?
        get() = findField(NxmFieldType.NICKNAME)?.data?.toString(Charsets.UTF_8)

    val replyToHex: String?
        get() = findField(NxmFieldType.REPLY_TO)?.data?.joinToString("") { "%02X".format(it) }

    val reaction: String?
        get() = findField(NxmFieldType.REACTION)?.data?.toString(Charsets.UTF_8)

    val title: String?
        get() = findField(NxmFieldType.TITLE)?.data?.toString(Charsets.UTF_8)

    val filename: String?
        get() = findField(NxmFieldType.FILENAME)?.data?.toString(Charsets.UTF_8)

    val mimetype: String?
        get() = findField(NxmFieldType.MIMETYPE)?.data?.toString(Charsets.UTF_8)

    val filedata: ByteArray?
        get() = findField(NxmFieldType.FILEDATA)?.data

    /** 0-based chunk index in a multi-part transfer (null = legacy single-message). */
    val partIdx: Int?
        get() = findField(NxmFieldType.PART_IDX)?.data?.takeIf { it.size >= 2 }?.let {
            (it[0].toInt() and 0xFF) or ((it[1].toInt() and 0xFF) shl 8)
        }

    /** Total chunks in this transfer. >1 means this message is one chunk of N. */
    val partTotal: Int?
        get() = findField(NxmFieldType.PART_TOTAL)?.data?.takeIf { it.size >= 2 }?.let {
            (it[0].toInt() and 0xFF) or ((it[1].toInt() and 0xFF) shl 8)
        }

    /** Duration in seconds (voice notes), parsed from DURATION field. */
    val durationSec: Int?
        get() = findField(NxmFieldType.DURATION)?.data?.takeIf { it.size >= 2 }?.let {
            (it[0].toInt() and 0xFF) or ((it[1].toInt() and 0xFF) shl 8)
        }

    /** Thumbnail bytes (images). */
    val thumbnail: ByteArray?
        get() = findField(NxmFieldType.THUMBNAIL)?.data

    /** CALL_STATE byte for VOICE_CALL messages. */
    val callState: Int?
        get() = findField(NxmFieldType.CALL_STATE)?.data?.firstOrNull()?.toInt()?.and(0xFF)

    /** CODEC byte (used in VOICE_CALL audio frames). */
    val codec: Int?
        get() = findField(NxmFieldType.CODEC)?.data?.firstOrNull()?.toInt()?.and(0xFF)
}
