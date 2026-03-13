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
    CONTACT(0x0C);

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
    SIGNATURE(0x12);

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

    val filename: String?
        get() = findField(NxmFieldType.FILENAME)?.data?.toString(Charsets.UTF_8)

    val mimetype: String?
        get() = findField(NxmFieldType.MIMETYPE)?.data?.toString(Charsets.UTF_8)

    val filedata: ByteArray?
        get() = findField(NxmFieldType.FILEDATA)?.data
}
