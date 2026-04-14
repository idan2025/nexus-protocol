package com.nexus.mesh.data

import kotlinx.coroutines.flow.Flow

class MessageRepository(private val db: NexusDatabase) {
    private val messageDao = db.messageDao()
    private val conversationDao = db.conversationDao()
    private val contactDao = db.contactDao()
    private val groupDao = db.groupDao()

    // --- Messages ---

    fun getMessages(peerAddr: String): Flow<List<MessageEntity>> =
        messageDao.getMessagesForPeer(peerAddr)

    fun getGroupMessages(groupId: String): Flow<List<MessageEntity>> =
        messageDao.getMessagesForGroup(groupId)

    suspend fun insertMessage(message: MessageEntity): Long {
        val id = messageDao.insert(message)
        updateConversation(message)
        return id
    }

    suspend fun updateDeliveryStatus(nxmMsgId: String, status: Int) {
        messageDao.updateDeliveryStatus(nxmMsgId, status)
    }

    suspend fun getUnreadIncomingForPeer(peerAddr: String): List<MessageEntity> =
        messageDao.getUnreadIncomingForPeer(peerAddr, DeliveryStatus.READ)

    suspend fun markIncomingRead(peerAddr: String) =
        messageDao.markIncomingRead(peerAddr, DeliveryStatus.READ)

    suspend fun findByNxmMsgId(msgId: String): MessageEntity? =
        messageDao.findByNxmMsgId(msgId)

    suspend fun deleteMessage(id: Long) = messageDao.deleteById(id)

    suspend fun clearMessages(peerAddr: String) = messageDao.deleteAllForPeer(peerAddr)

    // --- Conversations ---

    fun getConversations(): Flow<List<ConversationEntity>> = conversationDao.getAll()

    suspend fun getConversation(peerAddr: String): ConversationEntity? =
        conversationDao.getByAddr(peerAddr)

    suspend fun ensureConversation(peerAddr: String) {
        if (conversationDao.getByAddr(peerAddr) == null) {
            conversationDao.upsert(ConversationEntity(peerAddr = peerAddr))
        }
    }

    suspend fun setNickname(peerAddr: String, nickname: String?) {
        ensureConversation(peerAddr)
        conversationDao.updateNickname(peerAddr, nickname?.ifBlank { null })
    }

    suspend fun clearUnread(peerAddr: String) = conversationDao.clearUnread(peerAddr)

    suspend fun deleteConversation(peerAddr: String) {
        messageDao.deleteAllForPeer(peerAddr)
        conversationDao.delete(peerAddr)
    }

    // --- Contacts ---

    fun getContacts(): Flow<List<ContactEntity>> = contactDao.getAll()

    suspend fun upsertContact(contact: ContactEntity) = contactDao.upsert(contact)

    // --- Groups ---

    fun getGroups(): Flow<List<GroupEntity>> = groupDao.getAll()

    suspend fun getGroup(groupId: String): GroupEntity? = groupDao.getById(groupId)

    suspend fun ensureGroup(groupId: String, name: String? = null) {
        if (groupDao.getById(groupId) == null) {
            groupDao.upsert(GroupEntity(groupId = groupId, name = name))
        }
    }

    suspend fun upsertGroup(group: GroupEntity) = groupDao.upsert(group)

    suspend fun setGroupName(groupId: String, name: String?) {
        ensureGroup(groupId)
        groupDao.updateName(groupId, name)
    }

    suspend fun clearGroupUnread(groupId: String) = groupDao.clearUnread(groupId)

    suspend fun deleteGroup(groupId: String) {
        messageDao.deleteAllForGroup(groupId)
        groupDao.removeAllMembers(groupId)
        groupDao.delete(groupId)
    }

    suspend fun getGroupMembers(groupId: String): List<GroupMemberEntity> =
        groupDao.getMembers(groupId)

    suspend fun addGroupMember(groupId: String, address: String) {
        groupDao.insertMember(GroupMemberEntity(groupId = groupId, address = address))
    }

    suspend fun removeGroupMember(groupId: String, address: String) {
        groupDao.removeMember(groupId, address)
    }

    suspend fun insertGroupMessage(message: MessageEntity): Long {
        val id = messageDao.insert(message)
        updateGroupConversation(message)
        return id
    }

    private suspend fun updateGroupConversation(message: MessageEntity) {
        val groupId = message.groupId ?: return
        val existing = groupDao.getById(groupId)
        val preview = if (message.text.length > 50) message.text.take(50) + "..." else message.text
        val unread = if (message.isOutgoing) (existing?.unreadCount ?: 0)
                     else (existing?.unreadCount ?: 0) + 1

        groupDao.upsert(
            GroupEntity(
                groupId = groupId,
                name = existing?.name,
                createdAt = existing?.createdAt ?: System.currentTimeMillis(),
                lastMessageTime = message.timestamp,
                lastMessagePreview = preview,
                unreadCount = unread
            )
        )
    }

    // --- Private helpers ---

    private suspend fun updateConversation(message: MessageEntity) {
        val peerAddr = message.peerAddr
        val existing = conversationDao.getByAddr(peerAddr)
        val preview = if (message.text.length > 50) message.text.take(50) + "..." else message.text
        val unread = if (message.isOutgoing) (existing?.unreadCount ?: 0)
                     else (existing?.unreadCount ?: 0) + 1

        conversationDao.upsert(
            ConversationEntity(
                peerAddr = peerAddr,
                nickname = existing?.nickname,
                lastMessageTime = message.timestamp,
                unreadCount = unread,
                lastMessagePreview = preview
            )
        )
    }
}
