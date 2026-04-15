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

    fun buildImage(filename: String, data: ByteArray,
                   thumbnail: ByteArray? = null): ByteArray {
        val fields = mutableListOf<Pair<NxmFieldType, ByteArray>>()
        fields.add(NxmFieldType.MSG_ID to generateMsgId())
        fields.add(NxmFieldType.FILENAME to filename.toByteArray(Charsets.UTF_8))
        fields.add(NxmFieldType.MIMETYPE to "image/jpeg".toByteArray(Charsets.UTF_8))
        fields.add(NxmFieldType.FILEDATA to data)
        if (thumbnail != null) {
            fields.add(NxmFieldType.THUMBNAIL to thumbnail)
        }
        return serialize(NxmType.IMAGE, 0, fields)
    }

    fun buildFile(filename: String, mimetype: String, data: ByteArray): ByteArray {
        val fields = mutableListOf<Pair<NxmFieldType, ByteArray>>()
        fields.add(NxmFieldType.MSG_ID to generateMsgId())
        fields.add(NxmFieldType.FILENAME to filename.toByteArray(Charsets.UTF_8))
        fields.add(NxmFieldType.MIMETYPE to mimetype.toByteArray(Charsets.UTF_8))
        fields.add(NxmFieldType.FILEDATA to data)
        return serialize(NxmType.FILE, 0, fields)
    }

    fun buildVoice(data: ByteArray, durationSec: Int, codec: Int = 0): ByteArray {
        val fields = mutableListOf<Pair<NxmFieldType, ByteArray>>()
        fields.add(NxmFieldType.MSG_ID to generateMsgId())
        fields.add(NxmFieldType.FILEDATA to data)
        fields.add(NxmFieldType.DURATION to
            ByteBuffer.allocate(2).order(ByteOrder.LITTLE_ENDIAN).putShort(durationSec.toShort()).array())
        fields.add(NxmFieldType.CODEC to byteArrayOf((codec and 0xFF).toByte()))
        return serialize(NxmType.VOICE_NOTE, 0, fields)
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
