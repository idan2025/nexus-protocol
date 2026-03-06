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
    }

    interface Callback {
        fun onData(src: ByteArray, data: ByteArray)
        fun onNeighbor(addr: ByteArray, role: Int)
        fun onSession(src: ByteArray, data: ByteArray)
    }

    var isRunning = false
        private set

    fun init(role: Int = ROLE_LEAF, callback: Callback): Boolean {
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
}
