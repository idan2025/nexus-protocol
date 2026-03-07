package com.nexus.mesh.service

import android.app.*
import android.content.Context
import android.content.Intent
import android.net.wifi.WifiManager
import android.os.Binder
import android.os.IBinder
import android.util.Log
import androidx.core.app.NotificationCompat
import com.nexus.mesh.R
import kotlinx.coroutines.*
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow

class NexusService : Service(), NexusNode.Callback {
    companion object {
        private const val TAG = "NexusService"
        private const val CHANNEL_ID = "nexus_mesh"
        private const val NOTIFICATION_ID = 1
        private const val PREFS_NAME = "nexus_identity"
        private const val KEY_IDENTITY = "identity_bytes"
        private const val PREFS_TCP = "nexus_tcp"
        private const val KEY_TCP_ENABLED = "tcp_enabled"
        private const val KEY_TCP_PORT = "tcp_listen_port"
        private const val KEY_TCP_PEERS = "tcp_peers"
        private const val PREFS_NICKNAMES = "nexus_nicknames"
        private const val KEY_MY_NAME = "my_name"
    }

    private val node = NexusNode()
    private val binder = LocalBinder()
    private var pollJob: Job? = null
    private val scope = CoroutineScope(Dispatchers.IO + SupervisorJob())
    private var multicastLock: WifiManager.MulticastLock? = null

    // --- Data models ---

    data class ChatMessage(
        val id: Long,
        val peerAddr: String,
        val text: String,
        val timestamp: Long,
        val isOutgoing: Boolean,
        val isDirect: Boolean = true  // true if peer is direct neighbor
    )

    data class Neighbor(val addr: String, val role: Int)

    data class RouteInfo(
        val hopCount: Int,
        val viaTransport: Int,
        val nextHop: String,
        val isDirect: Boolean
    )

    // --- Observable state ---

    private val _conversations = MutableStateFlow<Map<String, List<ChatMessage>>>(emptyMap())
    val conversations: StateFlow<Map<String, List<ChatMessage>>> = _conversations

    private val _neighbors = MutableStateFlow<List<Neighbor>>(emptyList())
    val neighbors: StateFlow<List<Neighbor>> = _neighbors

    private val _address = MutableStateFlow("--------")
    val address: StateFlow<String> = _address

    private val _tcpActive = MutableStateFlow(false)
    val tcpActive: StateFlow<Boolean> = _tcpActive

    private val _udpActive = MutableStateFlow(false)
    val udpActive: StateFlow<Boolean> = _udpActive

    private val _nicknames = MutableStateFlow<Map<String, String>>(emptyMap())
    val nicknames: StateFlow<Map<String, String>> = _nicknames

    private val _myName = MutableStateFlow("")
    val myName: StateFlow<String> = _myName

    private var nextMsgId = 1L

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
        node.stopTcpInet()
        node.stopUdpMulticast()
        multicastLock?.release()
        node.stop()
        scope.cancel()
        super.onDestroy()
    }

    private fun startNode() {
        val prefs = getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)
        val savedBytes = prefs.getString(KEY_IDENTITY, null)

        val ok = if (savedBytes != null) {
            val bytes = android.util.Base64.decode(savedBytes, android.util.Base64.DEFAULT)
            node.initWithIdentity(NexusNode.ROLE_RELAY, bytes, this)
        } else {
            node.init(NexusNode.ROLE_RELAY, this)
        }

        if (!ok) {
            Log.e(TAG, "Failed to start node")
            stopSelf()
            return
        }

        node.getIdentityBytes()?.let { bytes ->
            val encoded = android.util.Base64.encodeToString(bytes, android.util.Base64.DEFAULT)
            prefs.edit().putString(KEY_IDENTITY, encoded).apply()
        }

        _address.value = node.getAddressHex()
        loadNicknames()
        updateNotification("Node: ${_address.value}")

        pollJob = scope.launch {
            while (isActive) {
                node.poll(50)
                delay(50)
            }
        }

        scope.launch {
            while (isActive) {
                delay(30_000)
                node.announce()
            }
        }

        val tcpPrefs = getSharedPreferences(PREFS_TCP, Context.MODE_PRIVATE)
        if (tcpPrefs.getBoolean(KEY_TCP_ENABLED, false)) {
            val port = tcpPrefs.getInt(KEY_TCP_PORT, 4242)
            val peersStr = tcpPrefs.getString(KEY_TCP_PEERS, "") ?: ""
            startTcpInetFromConfig(port, peersStr)
        }

        startUdpMulticast()

        Log.i(TAG, "Node started: ${_address.value}")
    }

    private fun startTcpInetFromConfig(port: Int, peersStr: String) {
        val peerList = if (peersStr.isNotEmpty()) {
            peersStr.split(",").mapNotNull { peer ->
                val parts = peer.trim().split(":")
                if (parts.size == 2) parts[0] to parts[1].toIntOrNull()
                else null
            }.filter { it.second != null }
        } else emptyList()

        val hosts = peerList.map { it.first }.toTypedArray()
        val ports = peerList.map { it.second!! }.toIntArray()

        val ok = node.startTcpInet(port, hosts, ports)
        _tcpActive.value = ok
        if (ok) {
            Log.i(TAG, "TCP inet started (port=$port, peers=${peerList.size})")
            updateTransportNotification()
        } else {
            Log.w(TAG, "TCP inet failed to start")
        }
    }

    // --- NexusNode.Callback ---

    override fun onData(src: ByteArray, data: ByteArray) {
        val srcHex = src.joinToString("") { "%02X".format(it) }
        Log.i(TAG, "Data from $srcHex: ${data.size} bytes")

        val text = String(data, Charsets.UTF_8)
        val isDirect = node.isNeighbor(src) >= 0
        addMessage(srcHex, ChatMessage(
            nextMsgId++, srcHex, text, System.currentTimeMillis(),
            isOutgoing = false, isDirect = isDirect
        ))
        showMessageNotification(srcHex, text)
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

        val text = String(data, Charsets.UTF_8)
        val isDirect = node.isNeighbor(src) >= 0
        addMessage(srcHex, ChatMessage(
            nextMsgId++, srcHex, text, System.currentTimeMillis(),
            isOutgoing = false, isDirect = isDirect
        ))
        showMessageNotification(srcHex, text)
    }

    // --- Conversation management ---

    private fun addMessage(peerAddr: String, msg: ChatMessage) {
        val current = _conversations.value.toMutableMap()
        current[peerAddr] = (current[peerAddr] ?: emptyList()) + msg
        _conversations.value = current
    }

    // --- Public API for UI ---

    fun sendMessage(dest: String, text: String): Boolean {
        val destBytes = hexToBytes(dest) ?: return false
        val ok = node.send(destBytes, text.toByteArray(Charsets.UTF_8))
        if (ok) {
            val isDirect = node.isNeighbor(destBytes) >= 0
            addMessage(dest, ChatMessage(
                nextMsgId++, dest, text, System.currentTimeMillis(),
                isOutgoing = true, isDirect = isDirect
            ))
        }
        return ok
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

    // --- Delete operations ---

    fun deleteConversation(peerAddr: String) {
        val current = _conversations.value.toMutableMap()
        current.remove(peerAddr)
        _conversations.value = current
    }

    fun deleteMessage(peerAddr: String, msgId: Long) {
        val current = _conversations.value.toMutableMap()
        val messages = current[peerAddr] ?: return
        val filtered = messages.filter { it.id != msgId }
        if (filtered.isEmpty()) {
            current.remove(peerAddr)
        } else {
            current[peerAddr] = filtered
        }
        _conversations.value = current
    }

    fun clearConversation(peerAddr: String) {
        val current = _conversations.value.toMutableMap()
        current[peerAddr] = emptyList()
        _conversations.value = current
    }

    // --- Route info ---

    fun getRouteInfo(addr: String): RouteInfo? {
        val destBytes = hexToBytes(addr) ?: return null
        val info = node.getRouteInfo(destBytes) ?: return null
        // info = [hop_count, via_transport, nh_b0, nh_b1, nh_b2, nh_b3]
        val nextHop = "%02X%02X%02X%02X".format(
            info[2] and 0xFF, info[3] and 0xFF,
            info[4] and 0xFF, info[5] and 0xFF
        )
        return RouteInfo(
            hopCount = info[0],
            viaTransport = info[1],
            nextHop = nextHop,
            isDirect = info[0] <= 1
        )
    }

    fun isPeerNeighbor(addr: String): Boolean {
        val bytes = hexToBytes(addr) ?: return false
        return node.isNeighbor(bytes) >= 0
    }

    // --- TCP Internet Transport ---

    fun startTcpInet(listenPort: Int, peers: String): Boolean {
        val tcpPrefs = getSharedPreferences(PREFS_TCP, Context.MODE_PRIVATE)
        tcpPrefs.edit()
            .putBoolean(KEY_TCP_ENABLED, true)
            .putInt(KEY_TCP_PORT, listenPort)
            .putString(KEY_TCP_PEERS, peers)
            .apply()

        startTcpInetFromConfig(listenPort, peers)
        return _tcpActive.value
    }

    fun stopTcpInet() {
        node.stopTcpInet()
        _tcpActive.value = false

        val tcpPrefs = getSharedPreferences(PREFS_TCP, Context.MODE_PRIVATE)
        tcpPrefs.edit().putBoolean(KEY_TCP_ENABLED, false).apply()

        updateTransportNotification()
        Log.i(TAG, "TCP inet stopped")
    }

    fun getTcpConfig(): Pair<Int, String> {
        val tcpPrefs = getSharedPreferences(PREFS_TCP, Context.MODE_PRIVATE)
        return Pair(
            tcpPrefs.getInt(KEY_TCP_PORT, 4242),
            tcpPrefs.getString(KEY_TCP_PEERS, "") ?: ""
        )
    }

    // --- UDP Multicast ---

    fun startUdpMulticast(): Boolean {
        if (multicastLock == null) {
            val wifi = applicationContext.getSystemService(Context.WIFI_SERVICE) as? WifiManager
            multicastLock = wifi?.createMulticastLock("nexus_mcast")?.apply {
                setReferenceCounted(false)
            }
        }
        multicastLock?.acquire()

        val ok = node.startUdpMulticast()
        _udpActive.value = ok
        if (ok) {
            Log.i(TAG, "UDP multicast started")
            updateTransportNotification()
        } else {
            Log.w(TAG, "UDP multicast failed to start")
            multicastLock?.release()
        }
        return ok
    }

    fun stopUdpMulticast() {
        node.stopUdpMulticast()
        _udpActive.value = false
        multicastLock?.release()
        updateTransportNotification()
        Log.i(TAG, "UDP multicast stopped")
    }

    // --- Nicknames ---

    fun getDisplayName(addr: String): String {
        return _nicknames.value[addr] ?: addr
    }

    fun setNickname(addr: String, name: String) {
        val current = _nicknames.value.toMutableMap()
        if (name.isBlank()) {
            current.remove(addr)
        } else {
            current[addr] = name.trim()
        }
        _nicknames.value = current
        saveNicknames()
    }

    fun setMyName(name: String) {
        _myName.value = name.trim()
        val prefs = getSharedPreferences(PREFS_NICKNAMES, Context.MODE_PRIVATE)
        prefs.edit().putString(KEY_MY_NAME, name.trim()).apply()
    }

    private fun loadNicknames() {
        val prefs = getSharedPreferences(PREFS_NICKNAMES, Context.MODE_PRIVATE)
        _myName.value = prefs.getString(KEY_MY_NAME, "") ?: ""
        val all = prefs.all
        val nicks = mutableMapOf<String, String>()
        for ((key, value) in all) {
            if (key != KEY_MY_NAME && value is String) {
                nicks[key] = value
            }
        }
        _nicknames.value = nicks
    }

    private fun saveNicknames() {
        val prefs = getSharedPreferences(PREFS_NICKNAMES, Context.MODE_PRIVATE)
        val editor = prefs.edit()
        val myName = prefs.getString(KEY_MY_NAME, "") ?: ""
        editor.clear()
        editor.putString(KEY_MY_NAME, myName)
        for ((addr, name) in _nicknames.value) {
            editor.putString(addr, name)
        }
        editor.apply()
    }

    // --- Helpers ---

    private fun updateTransportNotification() {
        val parts = mutableListOf("Node: ${_address.value}")
        if (_tcpActive.value) parts.add("TCP")
        if (_udpActive.value) parts.add("UDP")
        updateNotification(parts.joinToString(" | "))
    }

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

    private fun showMessageNotification(from: String, text: String) {
        val displayName = getDisplayName(from)
        val notification = NotificationCompat.Builder(this, CHANNEL_ID)
            .setContentTitle("Message from $displayName")
            .setContentText(text.take(100))
            .setSmallIcon(R.drawable.ic_mesh)
            .setAutoCancel(true)
            .build()
        val mgr = getSystemService(NotificationManager::class.java)
        mgr.notify(from.hashCode(), notification)
    }
}
