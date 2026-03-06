package com.nexus.mesh.service

import android.app.*
import android.content.Context
import android.content.Intent
import android.os.Binder
import android.os.IBinder
import android.util.Log
import androidx.core.app.NotificationCompat
import com.nexus.mesh.R
import kotlinx.coroutines.*
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow

/**
 * Foreground service that keeps the NEXUS node running.
 * Handles identity persistence, polling loop, and message dispatch.
 */
class NexusService : Service(), NexusNode.Callback {
    companion object {
        private const val TAG = "NexusService"
        private const val CHANNEL_ID = "nexus_mesh"
        private const val NOTIFICATION_ID = 1
        private const val PREFS_NAME = "nexus_identity"
        private const val KEY_IDENTITY = "identity_bytes"
    }

    private val node = NexusNode()
    private val binder = LocalBinder()
    private var pollJob: Job? = null
    private val scope = CoroutineScope(Dispatchers.IO + SupervisorJob())

    // Observable state
    data class Message(val src: String, val data: ByteArray, val timestamp: Long)
    data class Neighbor(val addr: String, val role: Int)

    private val _messages = MutableStateFlow<List<Message>>(emptyList())
    val messages: StateFlow<List<Message>> = _messages

    private val _neighbors = MutableStateFlow<List<Neighbor>>(emptyList())
    val neighbors: StateFlow<List<Neighbor>> = _neighbors

    private val _address = MutableStateFlow("--------")
    val address: StateFlow<String> = _address

    inner class LocalBinder : Binder() {
        fun getService(): NexusService = this@NexusService
    }

    override fun onBind(intent: Intent?): IBinder = binder

    override fun onCreate() {
        super.onCreate()
        createNotificationChannel()
        startForeground(NOTIFICATION_ID, buildNotification("Starting..."))
        startNode()
    }

    override fun onDestroy() {
        pollJob?.cancel()
        node.stop()
        scope.cancel()
        super.onDestroy()
    }

    private fun startNode() {
        // Try to load saved identity
        val prefs = getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)
        val savedBytes = prefs.getString(KEY_IDENTITY, null)

        val ok = if (savedBytes != null) {
            val bytes = android.util.Base64.decode(savedBytes, android.util.Base64.DEFAULT)
            node.initWithIdentity(NexusNode.ROLE_LEAF, bytes, this)
        } else {
            node.init(NexusNode.ROLE_LEAF, this)
        }

        if (!ok) {
            Log.e(TAG, "Failed to start node")
            stopSelf()
            return
        }

        // Save identity for next launch
        node.getIdentityBytes()?.let { bytes ->
            val encoded = android.util.Base64.encodeToString(bytes, android.util.Base64.DEFAULT)
            prefs.edit().putString(KEY_IDENTITY, encoded).apply()
        }

        _address.value = node.getAddressHex()
        updateNotification("Node: ${_address.value}")

        // Start polling loop
        pollJob = scope.launch {
            while (isActive) {
                node.poll(50)
                delay(50)
            }
        }

        // Periodic announce
        scope.launch {
            while (isActive) {
                delay(30_000)
                node.announce()
            }
        }

        Log.i(TAG, "Node started: ${_address.value}")
    }

    // --- NexusNode.Callback ---

    override fun onData(src: ByteArray, data: ByteArray) {
        val srcHex = src.joinToString("") { "%02X".format(it) }
        Log.i(TAG, "Data from $srcHex: ${data.size} bytes")

        val msg = Message(srcHex, data.copyOf(), System.currentTimeMillis())
        _messages.value = _messages.value + msg

        showMessageNotification(srcHex, data)
    }

    override fun onNeighbor(addr: ByteArray, role: Int) {
        val addrHex = addr.joinToString("") { "%02X".format(it) }
        Log.i(TAG, "Neighbor: $addrHex role=$role")

        val existing = _neighbors.value.toMutableList()
        existing.removeAll { it.addr == addrHex }
        existing.add(Neighbor(addrHex, role))
        _neighbors.value = existing
    }

    override fun onSession(src: ByteArray, data: ByteArray) {
        val srcHex = src.joinToString("") { "%02X".format(it) }
        Log.i(TAG, "Session msg from $srcHex: ${data.size} bytes")

        val msg = Message(srcHex, data.copyOf(), System.currentTimeMillis())
        _messages.value = _messages.value + msg

        showMessageNotification(srcHex, data)
    }

    // --- Public API for UI ---

    fun sendMessage(dest: String, text: String): Boolean {
        val destBytes = hexToBytes(dest) ?: return false
        return node.send(destBytes, text.toByteArray(Charsets.UTF_8))
    }

    fun sendSessionMessage(dest: String, text: String): Boolean {
        val destBytes = hexToBytes(dest) ?: return false
        return node.sendSession(destBytes, text.toByteArray(Charsets.UTF_8))
    }

    fun startSession(dest: String): Boolean {
        val destBytes = hexToBytes(dest) ?: return false
        return node.sessionStart(destBytes)
    }

    fun getNodeAddress(): String = _address.value

    // --- Helpers ---

    private fun hexToBytes(hex: String): ByteArray? {
        if (hex.length != 8) return null
        return try {
            ByteArray(4) { hex.substring(it * 2, it * 2 + 2).toInt(16).toByte() }
        } catch (e: Exception) { null }
    }

    private fun createNotificationChannel() {
        val channel = NotificationChannel(
            CHANNEL_ID, "NEXUS Mesh",
            NotificationManager.IMPORTANCE_LOW
        ).apply {
            description = "NEXUS mesh node service"
        }
        val mgr = getSystemService(NotificationManager::class.java)
        mgr.createNotificationChannel(channel)
    }

    private fun buildNotification(text: String): Notification {
        return NotificationCompat.Builder(this, CHANNEL_ID)
            .setContentTitle("NEXUS Mesh")
            .setContentText(text)
            .setSmallIcon(R.drawable.ic_mesh)
            .setOngoing(true)
            .build()
    }

    private fun updateNotification(text: String) {
        val mgr = getSystemService(NotificationManager::class.java)
        mgr.notify(NOTIFICATION_ID, buildNotification(text))
    }

    private fun showMessageNotification(from: String, data: ByteArray) {
        val text = String(data, Charsets.UTF_8)
        val notification = NotificationCompat.Builder(this, CHANNEL_ID)
            .setContentTitle("Message from $from")
            .setContentText(text.take(100))
            .setSmallIcon(R.drawable.ic_mesh)
            .setAutoCancel(true)
            .build()
        val mgr = getSystemService(NotificationManager::class.java)
        mgr.notify(from.hashCode(), notification)
    }
}
