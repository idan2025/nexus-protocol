package com.nexus.mesh.data

import androidx.room.Entity
import androidx.room.PrimaryKey

@Entity(tableName = "contacts")
data class ContactEntity(
    @PrimaryKey
    val address: String,
    val nickname: String? = null,
    val signPubkey: String? = null,
    val x25519Pubkey: String? = null,
    val firstSeen: Long = System.currentTimeMillis(),
    val lastSeen: Long = System.currentTimeMillis(),
    val role: Int? = null,
    val trustLevel: Int = ContactTrust.UNKNOWN
)

object ContactTrust {
    const val UNKNOWN = 0
    const val SEEN = 1
    const val VERIFIED = 2
}

object ContactRole {
    fun isClientVisible(role: Int?): Boolean = role == null || role in 0..2
}
