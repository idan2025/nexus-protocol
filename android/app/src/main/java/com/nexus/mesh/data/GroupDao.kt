package com.nexus.mesh.data

import androidx.room.*
import kotlinx.coroutines.flow.Flow

@Dao
interface GroupDao {
    @Query("SELECT * FROM groups ORDER BY lastMessageTime DESC")
    fun getAll(): Flow<List<GroupEntity>>

    @Query("SELECT * FROM groups WHERE groupId = :groupId")
    suspend fun getById(groupId: String): GroupEntity?

    @Insert(onConflict = OnConflictStrategy.REPLACE)
    suspend fun upsert(group: GroupEntity)

    @Query("DELETE FROM groups WHERE groupId = :groupId")
    suspend fun delete(groupId: String)

    @Query("UPDATE groups SET name = :name WHERE groupId = :groupId")
    suspend fun updateName(groupId: String, name: String?)

    @Query("UPDATE groups SET unreadCount = 0 WHERE groupId = :groupId")
    suspend fun clearUnread(groupId: String)

    // Members
    @Query("SELECT * FROM group_members WHERE groupId = :groupId")
    suspend fun getMembers(groupId: String): List<GroupMemberEntity>

    @Insert(onConflict = OnConflictStrategy.REPLACE)
    suspend fun insertMember(member: GroupMemberEntity)

    @Query("DELETE FROM group_members WHERE groupId = :groupId AND address = :address")
    suspend fun removeMember(groupId: String, address: String)

    @Query("DELETE FROM group_members WHERE groupId = :groupId")
    suspend fun removeAllMembers(groupId: String)
}
