package com.nexus.mesh.data

import androidx.room.Entity
import androidx.room.PrimaryKey

@Entity(tableName = "groups")
data class GroupEntity(
    @PrimaryKey
    val groupId: String,
    val name: String? = null,
    val createdAt: Long = System.currentTimeMillis(),
    val lastMessageTime: Long = 0,
    val lastMessagePreview: String = "",
    val unreadCount: Int = 0
)
