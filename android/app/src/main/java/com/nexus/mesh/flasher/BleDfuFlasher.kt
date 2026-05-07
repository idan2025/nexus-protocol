package com.nexus.mesh.flasher

import android.content.Context
import android.util.Log
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import no.nordicsemi.android.dfu.DfuProgressListener
import no.nordicsemi.android.dfu.DfuServiceController
import no.nordicsemi.android.dfu.DfuServiceInitiator
import no.nordicsemi.android.dfu.DfuServiceListenerHelper
import java.io.File

/**
 * Flashes nRF52 boards (RAK4631, XIAO nRF52840) via Nordic Buttonless
 * DFU over BLE.
 *
 * Adafruit's nRF52 BSP bootloader exposes the Buttonless DFU service
 * out of the box, so the user doesn't have to manually enter DFU mode:
 *   1. App writes 0x01 to the Buttonless characteristic on the live
 *      node -> node reboots into the DFU bootloader
 *   2. Bootloader advertises as "DfuTarg"
 *   3. DfuService uploads the .zip image, node reboots into the new
 *      firmware
 *
 * The .zip is the Nordic DFU package format (init packet + image +
 * manifest). CI generates it via `nrfutil pkg generate`.
 */
class BleDfuFlasher(private val context: Context) {

    sealed class State {
        data object Idle : State()
        data class Connecting(val deviceAddress: String) : State()
        data class EnablingDfu(val pct: Int) : State()
        data class Uploading(val pct: Int, val avgSpeedBps: Float) : State()
        data object Validating : State()
        data object Done : State()
        data class Error(val code: Int, val msg: String) : State()
    }

    private val _state = MutableStateFlow<State>(State.Idle)
    val state: StateFlow<State> = _state

    private var controller: DfuServiceController? = null

    private val progressListener = object : DfuProgressListener {
        override fun onDeviceConnecting(deviceAddress: String) {
            _state.value = State.Connecting(deviceAddress)
        }
        override fun onDeviceConnected(deviceAddress: String) {}
        override fun onDfuProcessStarting(deviceAddress: String) {
            _state.value = State.EnablingDfu(0)
        }
        override fun onDfuProcessStarted(deviceAddress: String) {
            _state.value = State.EnablingDfu(100)
        }
        override fun onEnablingDfuMode(deviceAddress: String) {
            _state.value = State.EnablingDfu(50)
        }
        override fun onProgressChanged(
            deviceAddress: String, percent: Int, speed: Float,
            avgSpeed: Float, currentPart: Int, partsTotal: Int
        ) {
            _state.value = State.Uploading(percent, avgSpeed)
        }
        override fun onFirmwareValidating(deviceAddress: String) {
            _state.value = State.Validating
        }
        override fun onDeviceDisconnecting(deviceAddress: String?) {}
        override fun onDeviceDisconnected(deviceAddress: String) {}
        override fun onDfuCompleted(deviceAddress: String) {
            _state.value = State.Done
            controller = null
        }
        override fun onDfuAborted(deviceAddress: String) {
            _state.value = State.Error(-1, "Aborted by user")
            controller = null
        }
        override fun onError(deviceAddress: String, error: Int, errorType: Int, message: String?) {
            Log.w(TAG, "DFU error $error/$errorType: $message")
            _state.value = State.Error(error, message ?: "Unknown DFU error")
            controller = null
        }
    }

    fun start(deviceAddress: String, deviceName: String?, zipFile: File) {
        DfuServiceListenerHelper.registerProgressListener(context, progressListener)
        val initiator = DfuServiceInitiator(deviceAddress)
            .setDeviceName(deviceName ?: "NEXUS Node")
            .setKeepBond(true)
            .setForceDfu(false)
            .setPacketsReceiptNotificationsEnabled(true)
            .setPacketsReceiptNotificationsValue(12)
            .setPrepareDataObjectDelay(300)
            .setUnsafeExperimentalButtonlessServiceInSecureDfuEnabled(true)
        initiator.setZip(android.net.Uri.fromFile(zipFile), zipFile.absolutePath)
        controller = initiator.start(context, no.nordicsemi.android.dfu.DfuService::class.java)
        // Foreground notification needs a content activity so the user
        // can return to the app from the system notification.
        DfuServiceInitiator.createDfuNotificationChannel(context)
    }

    fun cancel() {
        controller?.abort()
    }

    fun reset() { _state.value = State.Idle }

    fun release() {
        try { DfuServiceListenerHelper.unregisterProgressListener(context, progressListener) }
        catch (_: Exception) {}
    }

    companion object { private const val TAG = "BleDfuFlasher" }
}
