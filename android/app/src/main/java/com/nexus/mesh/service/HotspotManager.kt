package com.nexus.mesh.service

import android.content.Context
import android.net.wifi.WifiManager
import android.os.Build
import android.util.Log
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow

class HotspotManager(private val context: Context) {

    companion object {
        private const val TAG = "HotspotManager"
    }

    data class HotspotInfo(
        val ssid: String,
        val passphrase: String
    )

    private val _state = MutableStateFlow<HotspotState>(HotspotState.Off)
    val state: StateFlow<HotspotState> = _state

    private var reservation: WifiManager.LocalOnlyHotspotReservation? = null

    sealed class HotspotState {
        object Off : HotspotState()
        object Starting : HotspotState()
        data class On(val info: HotspotInfo) : HotspotState()
        data class Failed(val reason: String) : HotspotState()
    }

    fun start() {
        if (_state.value is HotspotState.On || _state.value is HotspotState.Starting) return
        _state.value = HotspotState.Starting

        val wifiManager = context.applicationContext
            .getSystemService(Context.WIFI_SERVICE) as? WifiManager
        if (wifiManager == null) {
            _state.value = HotspotState.Failed("No WiFi manager")
            return
        }

        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.O) {
            _state.value = HotspotState.Failed("Requires Android 8.0+")
            return
        }

        try {
            wifiManager.startLocalOnlyHotspot(object : WifiManager.LocalOnlyHotspotCallback() {
                override fun onStarted(r: WifiManager.LocalOnlyHotspotReservation) {
                    reservation = r
                    val config = r.wifiConfiguration
                    val ssid = config?.SSID ?: "NEXUS"
                    val pass = config?.preSharedKey ?: ""
                    Log.i(TAG, "Hotspot started: ssid=$ssid")
                    _state.value = HotspotState.On(HotspotInfo(ssid, pass))
                }

                override fun onStopped() {
                    Log.i(TAG, "Hotspot stopped")
                    reservation = null
                    _state.value = HotspotState.Off
                }

                override fun onFailed(reason: Int) {
                    Log.w(TAG, "Hotspot failed: reason=$reason")
                    reservation = null
                    _state.value = HotspotState.Failed("Error $reason")
                }
            }, null)
        } catch (e: Exception) {
            Log.e(TAG, "startLocalOnlyHotspot threw", e)
            _state.value = HotspotState.Failed(e.message ?: "Unknown error")
        }
    }

    fun stop() {
        reservation?.close()
        reservation = null
        _state.value = HotspotState.Off
    }
}
