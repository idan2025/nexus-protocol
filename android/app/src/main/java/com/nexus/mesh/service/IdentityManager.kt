package com.nexus.mesh.service

import android.content.Context
import android.util.Base64
import android.util.Log
import androidx.security.crypto.EncryptedSharedPreferences
import androidx.security.crypto.MasterKey
import com.nexus.mesh.data.IdentityBackup
import com.nexus.mesh.data.IdentityRecord
import org.json.JSONArray
import org.json.JSONObject
import java.util.UUID

/**
 * Manages a list of named NEXUS identities stored in EncryptedSharedPreferences.
 *
 * Each identity is serialized as a JSON object. The active identity ID is
 * stored separately so switching identities is a single key write.
 *
 * Thread safety: all mutations must be called from a single thread (Service
 * main thread / coroutine). Reads are safe from any thread.
 */
class IdentityManager(context: Context) {

    companion object {
        private const val TAG = "IdentityManager"
        private const val PREFS_NAME = "nexus_identities"
        private const val KEY_LIST   = "identity_list"
        private const val KEY_ACTIVE = "active_identity_id"

        // Legacy single-identity key (nexus_identity_secure prefs)
        private const val PREFS_LEGACY_SECURE = "nexus_identity_secure"
        private const val KEY_LEGACY_IDENTITY  = "identity_bytes"
        private const val LEGACY_IDENTITY_NAME = "Default"
    }

    private val prefs: android.content.SharedPreferences

    init {
        val masterKey = MasterKey.Builder(context)
            .setKeyScheme(MasterKey.KeyScheme.AES256_GCM)
            .build()
        prefs = EncryptedSharedPreferences.create(
            context,
            PREFS_NAME,
            masterKey,
            EncryptedSharedPreferences.PrefKeyEncryptionScheme.AES256_SIV,
            EncryptedSharedPreferences.PrefValueEncryptionScheme.AES256_GCM
        )
        migrateLegacyIdentity(context)
    }

    /** Migrate the old single-identity prefs entry to the multi-identity list. */
    private fun migrateLegacyIdentity(context: Context) {
        if (getAll().isNotEmpty()) return  // already migrated
        val masterKey = MasterKey.Builder(context)
            .setKeyScheme(MasterKey.KeyScheme.AES256_GCM)
            .build()
        val legacyPrefs = EncryptedSharedPreferences.create(
            context,
            PREFS_LEGACY_SECURE,
            masterKey,
            EncryptedSharedPreferences.PrefKeyEncryptionScheme.AES256_SIV,
            EncryptedSharedPreferences.PrefValueEncryptionScheme.AES256_GCM
        )
        val encoded = legacyPrefs.getString(KEY_LEGACY_IDENTITY, null) ?: return
        val rec = IdentityRecord(
            id = UUID.randomUUID().toString(),
            name = LEGACY_IDENTITY_NAME,
            encodedBytes = encoded,
            createdAt = System.currentTimeMillis()
        )
        addRecord(rec)
        setActive(rec.id)
        Log.i(TAG, "Migrated legacy identity to multi-identity list as '${rec.name}'")
    }

    fun getAll(): List<IdentityRecord> {
        val json = prefs.getString(KEY_LIST, null) ?: return emptyList()
        return try {
            val arr = JSONArray(json)
            (0 until arr.length()).map { arr.getJSONObject(it).toRecord() }
        } catch (e: Exception) {
            Log.e(TAG, "Failed to parse identity list", e)
            emptyList()
        }
    }

    fun getActiveId(): String? = prefs.getString(KEY_ACTIVE, null)

    fun getActive(): IdentityRecord? {
        val id = getActiveId() ?: return getAll().firstOrNull()
        return getAll().find { it.id == id }
    }

    fun setActive(id: String) {
        prefs.edit().putString(KEY_ACTIVE, id).apply()
    }

    /** Create a brand-new identity (node generates keypair on init). */
    fun createNew(name: String): IdentityRecord {
        val newId = UUID.randomUUID().toString()
        val rec = IdentityRecord(
            id = newId,
            name = name.ifBlank { "Identity ${getAll().size + 1}" },
            encodedBytes = "",   // filled by NexusService after node.getIdentityBytes()
            createdAt = System.currentTimeMillis(),
            dbTag = newId.replace("-", "").take(12)
        )
        addRecord(rec)
        return rec
    }

    /** Store the identity bytes for a record that was created with createNew(). */
    fun saveIdentityBytes(id: String, bytes: ByteArray) {
        val encoded = Base64.encodeToString(bytes, Base64.DEFAULT)
        val list = getAll().map { if (it.id == id) it.copy(encodedBytes = encoded) else it }
        saveList(list)
    }

    /** Update the mesh address for a record after the node starts. */
    fun saveAddrHex(id: String, addrHex: String) {
        val list = getAll().map { if (it.id == id) it.copy(addrHex = addrHex) else it }
        saveList(list)
    }

    /** Import an encrypted backup blob and add it as a new named identity. */
    fun importIdentity(name: String, blob: String, passphrase: CharArray): IdentityRecord {
        val plain = IdentityBackup.decrypt(blob, passphrase)
        val encoded = Base64.encodeToString(plain, Base64.DEFAULT)
        plain.fill(0)
        val newId = UUID.randomUUID().toString()
        val rec = IdentityRecord(
            id = newId,
            name = name.ifBlank { "Imported ${getAll().size + 1}" },
            encodedBytes = encoded,
            createdAt = System.currentTimeMillis(),
            dbTag = newId.replace("-", "").take(12)
        )
        addRecord(rec)
        return rec
    }

    /** Export a named identity as an encrypted backup blob. */
    fun exportIdentity(id: String, passphrase: CharArray): String? {
        val rec = getAll().find { it.id == id } ?: return null
        if (rec.encodedBytes.isBlank()) return null
        val bytes = Base64.decode(rec.encodedBytes, Base64.DEFAULT)
        return IdentityBackup.encrypt(bytes, passphrase)
    }

    fun rename(id: String, newName: String) {
        val list = getAll().map { if (it.id == id) it.copy(name = newName) else it }
        saveList(list)
    }

    fun delete(id: String) {
        val list = getAll().filter { it.id != id }
        saveList(list)
        if (getActiveId() == id) {
            prefs.edit().remove(KEY_ACTIVE).apply()
        }
    }

    fun getIdentityBytes(id: String): ByteArray? {
        val rec = getAll().find { it.id == id } ?: return null
        if (rec.encodedBytes.isBlank()) return null
        return Base64.decode(rec.encodedBytes, Base64.DEFAULT)
    }

    private fun addRecord(rec: IdentityRecord) {
        val list = getAll().toMutableList()
        list.add(rec)
        saveList(list)
    }

    private fun saveList(list: List<IdentityRecord>) {
        val arr = JSONArray()
        list.forEach { arr.put(it.toJson()) }
        prefs.edit().putString(KEY_LIST, arr.toString()).apply()
    }

    private fun IdentityRecord.toJson(): JSONObject = JSONObject().apply {
        put("id", id)
        put("name", name)
        put("encodedBytes", encodedBytes)
        put("createdAt", createdAt)
        put("addrHex", addrHex)
        put("dbTag", dbTag)
    }

    private fun JSONObject.toRecord(): IdentityRecord = IdentityRecord(
        id = getString("id"),
        name = getString("name"),
        encodedBytes = optString("encodedBytes", ""),
        createdAt = optLong("createdAt", 0L),
        addrHex = optString("addrHex", ""),
        dbTag = optString("dbTag", "")
    )
}
