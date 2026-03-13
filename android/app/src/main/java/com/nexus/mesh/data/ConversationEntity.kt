package com.nexus.mesh.data

import androidx.room.Entity
import androidx.room.PrimaryKey

@Entity(tableName = "conversations")
data class ConversationEntity(
    @PrimaryKey
    val peerAddr: String,
    val nickname: String? = null,
    val lastMessageTime: Long = 0,
    val unreadCount: Int = 0,
    val lastMessagePreview: String = ""
)
