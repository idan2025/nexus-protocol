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
    }

    interface Callback {
        fun onData(src: ByteArray, data: ByteArray)
        fun onNeighbor(addr: ByteArray, role: Int)
        fun onSession(src: ByteArray, data: ByteArray)
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

    fun announce() {
        if (isRunning) nativeAnnounce()
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
    private external fun nativeAnnounce()
    private external fun nativeInjectPacket(packet: ByteArray)
    private external fun nativeStartTcpInet(listenPort: Int, peerHosts: Array<String>, peerPorts: IntArray, reconnectMs: Int): Boolean
    private external fun nativeStopTcpInet()
    private external fun nativeIsTcpInetActive(): Boolean
    private external fun nativeStartUdpMulticast(): Boolean
    private external fun nativeStopUdpMulticast()
    private external fun nativeIsUdpMulticastActive(): Boolean
    private external fun nativeGetRouteInfo(dest: ByteArray): IntArray?
    private external fun nativeIsNeighbor(addr: ByteArray): Int
}
