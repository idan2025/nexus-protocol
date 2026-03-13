package com.nexus.mesh.nxm

import java.nio.ByteBuffer
import java.nio.ByteOrder

object NxmParser {
    fun parse(data: ByteArray): NxmMessage? {
        if (data.size < NXM_HEADER_SIZE) return null

        val version = data[0].toInt() and 0xFF
        if (version != NXM_VERSION) return null

        val typeValue = data[1].toInt() and 0xFF
        val type = NxmType.fromValue(typeValue) ?: return null

        val flags = data[2].toInt() and 0xFF

        val buf = ByteBuffer.wrap(data, 3, 4).order(ByteOrder.LITTLE_ENDIAN)
        val timestamp = buf.getInt().toLong() and 0xFFFFFFFFL

        val fieldCount = data[7].toInt() and 0xFF
        if (fieldCount > NXM_MAX_FIELDS) return null

        val fields = mutableListOf<NxmField>()
        var pos = NXM_HEADER_SIZE

        for (i in 0 until fieldCount) {
            if (pos + NXM_FIELD_HEADER > data.size) return null

            val ftValue = data[pos].toInt() and 0xFF
            val ft = NxmFieldType.fromValue(ftValue) ?: return null

            val flen = ByteBuffer.wrap(data, pos + 1, 2)
                .order(ByteOrder.LITTLE_ENDIAN).getShort().toInt() and 0xFFFF
            pos += NXM_FIELD_HEADER

            if (pos + flen > data.size) return null

            fields.add(NxmField(ft, data.copyOfRange(pos, pos + flen)))
            pos += flen
        }

        return NxmMessage(version, type, flags, timestamp, fields)
    }

    fun isNxm(data: ByteArray): Boolean {
        return data.size >= NXM_HEADER_SIZE && (data[0].toInt() and 0xFF) == NXM_VERSION
    }
}
