package com.nexus.mesh.data

import androidx.room.Entity

@Entity(
    tableName = "group_members",
    primaryKeys = ["groupId", "address"]
)
data class GroupMemberEntity(
    val groupId: String,
    val address: String
)
