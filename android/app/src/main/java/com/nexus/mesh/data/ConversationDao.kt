package com.nexus.mesh.data

import androidx.room.*
import kotlinx.coroutines.flow.Flow

@Dao
interface ConversationDao {
    @Query("SELECT * FROM conversations ORDER BY lastMessageTime DESC")
    fun getAll(): Flow<List<ConversationEntity>>

    @Query("SELECT * FROM conversations WHERE peerAddr = :peerAddr LIMIT 1")
    suspend fun getByAddr(peerAddr: String): ConversationEntity?

    @Insert(onConflict = OnConflictStrategy.REPLACE)
    suspend fun upsert(conversation: ConversationEntity)

    @Query("UPDATE conversations SET nickname = :nickname WHERE peerAddr = :peerAddr")
    suspend fun updateNickname(peerAddr: String, nickname: String?)

    @Query("UPDATE conversations SET unreadCount = 0 WHERE peerAddr = :peerAddr")
    suspend fun clearUnread(peerAddr: String)

    @Query("DELETE FROM conversations WHERE peerAddr = :peerAddr")
    suspend fun delete(peerAddr: String)
}
