package com.nexus.mesh.data

data class IdentityRecord(
    val id: String,            // UUID (stable identifier across renames)
    val name: String,          // human-readable label chosen by the user
    val encodedBytes: String,  // Base64-encoded raw identity bytes
    val createdAt: Long,       // epoch ms
    val addrHex: String = "",  // 4-byte mesh address, filled after first init
    val dbTag: String = ""     // DB file suffix; "" = legacy "nexus_messages.db"
)
