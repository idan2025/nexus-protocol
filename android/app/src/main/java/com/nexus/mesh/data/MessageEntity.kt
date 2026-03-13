package com.nexus.mesh.data

import androidx.room.Entity
import androidx.room.Index
import androidx.room.PrimaryKey

@Entity(
    tableName = "messages",
    indices = [
        Index(value = ["peerAddr", "timestamp"]),
        Index(value = ["nxmMsgId"]),
        Index(value = ["groupId"])
    ]
)
data class MessageEntity(
    @PrimaryKey(autoGenerate = true)
    val id: Long = 0,
    val peerAddr: String,
    val text: String,
    val timestamp: Long,
    val isOutgoing: Boolean,
    val isDirect: Boolean = true,
    val deliveryStatus: Int = DeliveryStatus.SENT,
    val nxmMsgId: String? = null,
    val messageType: Int = MessageType.TEXT,
    val mediaPath: String? = null,
    val fileName: String? = null,
    val mimeType: String? = null,
    val duration: Int = 0,
    val latitude: Double? = null,
    val longitude: Double? = null,
    val groupId: String? = null
)

object DeliveryStatus {
    const val SENDING = 0
    const val SENT = 1
    const val DELIVERED = 2
    const val READ = 3
    const val FAILED = 4
}

object MessageType {
    const val TEXT = 1
    const val FILE = 2
    const val IMAGE = 3
    const val LOCATION = 4
    const val VOICE_NOTE = 5
    const val REACTION = 6
    const val ACK = 7
    const val READ_RECEIPT = 9
}
