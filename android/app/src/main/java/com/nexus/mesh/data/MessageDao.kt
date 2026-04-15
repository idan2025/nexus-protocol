package com.nexus.mesh.data

import androidx.room.*
import kotlinx.coroutines.flow.Flow

@Dao
interface MessageDao {
    @Query("SELECT * FROM messages WHERE peerAddr = :peerAddr AND groupId IS NULL ORDER BY timestamp ASC")
    fun getMessagesForPeer(peerAddr: String): Flow<List<MessageEntity>>

    @Query("SELECT * FROM messages WHERE groupId = :groupId ORDER BY timestamp ASC")
    fun getMessagesForGroup(groupId: String): Flow<List<MessageEntity>>

    @Query("SELECT * FROM messages WHERE nxmMsgId = :msgId LIMIT 1")
    suspend fun findByNxmMsgId(msgId: String): MessageEntity?

    @Insert
    suspend fun insert(message: MessageEntity): Long

    @Update
    suspend fun update(message: MessageEntity)

    @Query("UPDATE messages SET deliveryStatus = :status WHERE nxmMsgId = :msgId")
    suspend fun updateDeliveryStatus(msgId: String, status: Int)

    @Query("SELECT * FROM messages WHERE peerAddr = :peerAddr AND groupId IS NULL AND isOutgoing = 0 AND nxmMsgId IS NOT NULL AND deliveryStatus < :readStatus")
    suspend fun getUnreadIncomingForPeer(peerAddr: String, readStatus: Int): List<MessageEntity>

    @Query("UPDATE messages SET deliveryStatus = :readStatus WHERE peerAddr = :peerAddr AND groupId IS NULL AND isOutgoing = 0 AND deliveryStatus < :readStatus")
    suspend fun markIncomingRead(peerAddr: String, readStatus: Int)

    @Query("DELETE FROM messages WHERE id = :id")
    suspend fun deleteById(id: Long)

    @Query("DELETE FROM messages WHERE peerAddr = :peerAddr AND groupId IS NULL")
    suspend fun deleteAllForPeer(peerAddr: String)

    @Query("SELECT COUNT(*) FROM messages WHERE peerAddr = :peerAddr AND groupId IS NULL")
    suspend fun countForPeer(peerAddr: String): Int

    @Query("DELETE FROM messages WHERE groupId = :groupId")
    suspend fun deleteAllForGroup(groupId: String)

    @Query(
        "SELECT messages.* FROM messages " +
        "JOIN messages_fts ON messages.id = messages_fts.rowid " +
        "WHERE messages_fts MATCH :query " +
        "ORDER BY messages.timestamp DESC LIMIT :limit"
    )
    suspend fun searchMessages(query: String, limit: Int = 200): List<MessageEntity>
}
