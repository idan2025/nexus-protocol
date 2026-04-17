package com.nexus.mesh.service

import android.util.Log

/**
 * Kotlin wrapper around the libnexus JNI bridge.
 * Manages a NEXUS mesh node running in the Android process.
 */
class NexusNode {
    companion object {
        init {
            System.loadLibrary("nexus_jni")
        }
        private const val TAG = "NexusNode"
        const val ROLE_LEAF = 0
        const val ROLE_RELAY = 1
        const val ROLE_GATEWAY = 2
        const val ROLE_ANCHOR = 3
        const val ROLE_SENTINEL = 4
        const val ROLE_PILLAR = 5
        const val ROLE_VAULT = 6

        const val STAMP_OK            = 0
        const val STAMP_UNDER_DIFFICULTY = 1  // missing or leading zeros < min
        const val STAMP_INVALID       = 2     // present but hash mismatch

        @JvmStatic
        external fun nativeVerifyStamp(buf: ByteArray, minDifficulty: Int): Int
    }

    interface Callback {
        fun onData(src: ByteArray, data: ByteArray)
        fun onNeighbor(addr: ByteArray, role: Int)
        fun onSession(src: ByteArray, data: ByteArray)
        fun onGroup(groupId: ByteArray, src: ByteArray, data: ByteArray)
    }

    var isRunning = false
        private set

    fun init(role: Int = ROLE_RELAY, callback: Callback): Boolean {
        val ok = nativeInit(role, callback)
        isRunning = ok
        if (ok) Log.i(TAG, "Node initialized, addr=${getAddressHex()}")
        return ok
    }

    fun initWithIdentity(role: Int, identityBytes: ByteArray, callback: Callback): Boolean {
        val ok = nativeInitWithIdentity(role, identityBytes, callback)
        isRunning = ok
        return ok
    }

    fun stop() {
        if (isRunning) {
            nativeStop()
            isRunning = false
            Log.i(TAG, "Node stopped")
        }
    }

    fun poll(timeoutMs: Int = 10) {
        if (isRunning) nativePoll(timeoutMs)
    }

    fun getAddress(): ByteArray? = if (isRunning) nativeGetAddress() else null

    fun getAddressHex(): String {
        val addr = getAddress() ?: return "--------"
        return addr.joinToString("") { "%02X".format(it) }
    }

    fun getIdentityBytes(): ByteArray? = if (isRunning) nativeGetIdentityBytes() else null

    fun send(dest: ByteArray, data: ByteArray): Boolean {
        return isRunning && nativeSend(dest, data)
    }

    fun sessionStart(dest: ByteArray): Boolean {
        return isRunning && nativeSessionStart(dest)
    }

    fun sendSession(dest: ByteArray, data: ByteArray): Boolean {
        return isRunning && nativeSendSession(dest, data)
    }

    /** Auto-fragmenting send (up to NX_FRAG_MAX_MESSAGE = 3808 bytes). */
    fun sendLarge(dest: ByteArray, data: ByteArray): Boolean {
        return isRunning && nativeSendLarge(dest, data)
    }

    fun announce() {
        if (isRunning) nativeAnnounce()
    }

    /** Ask [target] (typically a Pillar) to replay any stored-and-forward
     *  packets it's holding for us. */
    fun requestInbox(target: ByteArray): Boolean {
        return isRunning && nativeRequestInbox(target)
    }

    // TCP Internet transport
    fun startTcpInet(
        listenPort: Int = 4242,
        peerHosts: Array<String> = emptyArray(),
        peerPorts: IntArray = IntArray(0),
        reconnectMs: Int = 5000
    ): Boolean {
        return isRunning && nativeStartTcpInet(listenPort, peerHosts, peerPorts, reconnectMs)
    }

    fun stopTcpInet() {
        if (isRunning) nativeStopTcpInet()
    }

    val isTcpInetActive: Boolean
        get() = isRunning && nativeIsTcpInetActive()

    // UDP Multicast (auto-discovery on all interfaces)
    fun startUdpMulticast(): Boolean {
        return isRunning && nativeStartUdpMulticast()
    }

    fun stopUdpMulticast() {
        if (isRunning) nativeStopUdpMulticast()
    }

    val isUdpMulticastActive: Boolean
        get() = isRunning && nativeIsUdpMulticastActive()

    // Route/neighbor info
    fun getRouteInfo(dest: ByteArray): IntArray? {
        return if (isRunning) nativeGetRouteInfo(dest) else null
    }

    fun isNeighbor(addr: ByteArray): Int {
        return if (isRunning) nativeIsNeighbor(addr) else -1
    }

    // Sign pubkey for QR code
    fun getSignPubkey(): ByteArray? = if (isRunning) nativeGetSignPubkey() else null

    // Group operations
    fun groupCreate(groupId: ByteArray, key: ByteArray): Boolean {
        return isRunning && nativeGroupCreate(groupId, key)
    }

    fun groupAddMember(groupId: ByteArray, memberAddr: ByteArray): Boolean {
        return isRunning && nativeGroupAddMember(groupId, memberAddr)
    }

    fun groupSend(groupId: ByteArray, data: ByteArray): Boolean {
        return isRunning && nativeGroupSend(groupId, data)
    }

    fun groupList(): Array<ByteArray>? = if (isRunning) nativeGroupList() else null

    fun groupGetMembers(groupId: ByteArray): Array<ByteArray>? {
        return if (isRunning) nativeGroupGetMembers(groupId) else null
    }

    // Native methods
    private external fun nativeInit(role: Int, callback: Callback): Boolean
    private external fun nativeInitWithIdentity(role: Int, identity: ByteArray, callback: Callback): Boolean
    private external fun nativeStop()
    private external fun nativePoll(timeoutMs: Int)
    private external fun nativeGetAddress(): ByteArray
    private external fun nativeGetIdentityBytes(): ByteArray
    private external fun nativeSend(dest: ByteArray, data: ByteArray): Boolean
    private external fun nativeSessionStart(dest: ByteArray): Boolean
    private external fun nativeSendSession(dest: ByteArray, data: ByteArray): Boolean
    private external fun nativeSendLarge(dest: ByteArray, data: ByteArray): Boolean
    private external fun nativeAnnounce()
    private external fun nativeRequestInbox(target: ByteArray): Boolean
    private external fun nativeInjectPacket(packet: ByteArray)
    private external fun nativeStartTcpInet(listenPort: Int, peerHosts: Array<String>, peerPorts: IntArray, reconnectMs: Int): Boolean
    private external fun nativeStopTcpInet()
    private external fun nativeIsTcpInetActive(): Boolean
    private external fun nativeStartUdpMulticast(): Boolean
    private external fun nativeStopUdpMulticast()
    private external fun nativeIsUdpMulticastActive(): Boolean
    private external fun nativeGetTelemetry(): IntArray?
    private external fun nativeListRoutes(): Array<IntArray>
    private external fun nativeListNeighbors(): Array<IntArray>

    data class RouteRow(
        val dest: String,
        val nextHop: String,
        val hopCount: Int,
        val metric: Int,
        val viaTransport: Int,
        val expiresInSec: Int,
    )

    data class NeighborRow(
        val addr: String,
        val role: Int,
        val rssi: Int,
        val linkQuality: Int,
        val ageSec: Int,
    )

    private fun bytesToHex4(v: IntArray, off: Int): String =
        "%02X%02X%02X%02X".format(
            v[off] and 0xFF, v[off + 1] and 0xFF,
            v[off + 2] and 0xFF, v[off + 3] and 0xFF
        )

    fun listRoutes(): List<RouteRow> = nativeListRoutes().map { v ->
        RouteRow(
            dest = bytesToHex4(v, 0),
            nextHop = bytesToHex4(v, 4),
            hopCount = v[8],
            metric = v[9],
            viaTransport = v[10],
            expiresInSec = v[11],
        )
    }

    fun listNeighbors(): List<NeighborRow> = nativeListNeighbors().map { v ->
        NeighborRow(
            addr = bytesToHex4(v, 0),
            role = v[4],
            rssi = v[5],
            linkQuality = v[6],
            ageSec = v[7],
        )
    }

    data class Telemetry(
        val neighbors: Int,
        val routesActive: Int,
        val routesMax: Int,
        val anchorUsed: Int,
        val anchorMax: Int,
        val sessions: Int,
        val sessionsMax: Int,
        val transports: Int,
        val role: Int,
        val running: Boolean,
    )

    fun getTelemetry(): Telemetry? {
        val v = nativeGetTelemetry() ?: return null
        if (v.size < 10) return null
        return Telemetry(
            neighbors = v[0],
            routesActive = v[1],
            routesMax = v[2],
            anchorUsed = v[3],
            anchorMax = v[4],
            sessions = v[5],
            sessionsMax = v[6],
            transports = v[7],
            role = v[8],
            running = v[9] == 1,
        )
    }
    private external fun nativeGetRouteInfo(dest: ByteArray): IntArray?
    private external fun nativeIsNeighbor(addr: ByteArray): Int
    private external fun nativeGetSignPubkey(): ByteArray
    private external fun nativeGroupCreate(groupId: ByteArray, key: ByteArray): Boolean
    private external fun nativeGroupAddMember(groupId: ByteArray, memberAddr: ByteArray): Boolean
    private external fun nativeGroupSend(groupId: ByteArray, data: ByteArray): Boolean
    private external fun nativeGroupList(): Array<ByteArray>
    private external fun nativeGroupGetMembers(groupId: ByteArray): Array<ByteArray>
}
