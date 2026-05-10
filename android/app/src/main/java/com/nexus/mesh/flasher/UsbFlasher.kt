package com.nexus.mesh.flasher

import android.app.PendingIntent
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.content.BroadcastReceiver
import android.hardware.usb.UsbManager
import android.os.Build
import android.util.Log
import com.hoho.android.usbserial.driver.UsbSerialDriver
import com.hoho.android.usbserial.driver.UsbSerialPort
import com.hoho.android.usbserial.driver.UsbSerialProber
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.suspendCancellableCoroutine
import kotlinx.coroutines.withContext
import java.io.File
import kotlin.coroutines.resume

/**
 * Orchestrates the on-phone USB-OTG flashing flow for ESP32 boards.
 *
 * Flow:
 *   1. enumerate() lists available USB serial drivers
 *   2. requestPermission() pops the system permission dialog if the
 *      app doesn't already have access to the USB device
 *   3. flash() opens the port, syncs the chip, writes the image, resets
 *
 * Heltec V3 (CP210x) is the supported tier-1 device; XIAO ESP32-S3
 * (native USB-Serial-JTAG, VID 303A) is best-effort: the upstream
 * usb-serial-for-android probe table doesn't always recognise the JTAG
 * descriptor, so the user may need to put the chip into ROM bootloader
 * via the BOOT+RESET sequence first.
 */
class UsbFlasher(private val context: Context) {

    sealed class State {
        data object Idle : State()
        data class Detected(val drivers: List<UsbSerialDriver>) : State()
        data object Connecting : State()
        data object Syncing : State()
        data class Flashing(val block: Int, val total: Int) : State()
        data object Done : State()
        data class Error(val msg: String) : State()
    }

    private val _state = MutableStateFlow<State>(State.Idle)
    val state: StateFlow<State> = _state

    fun enumerate(): List<UsbSerialDriver> {
        val mgr = context.getSystemService(Context.USB_SERVICE) as UsbManager
        val drivers = UsbSerialProber.getDefaultProber().findAllDrivers(mgr).toMutableList()
        // Try the additional CDC-ACM probers too — covers boards with
        // generic CDC descriptors (e.g. some XIAO ESP32-S3 firmware
        // states).
        drivers += UsbSerialProber(com.hoho.android.usbserial.driver.ProbeTable().apply {
            // VID 0x303A (Espressif) — accept any product as CDC-ACM.
            for (pid in 0x1000..0x10FF) {
                addProduct(0x303A, pid, com.hoho.android.usbserial.driver.CdcAcmSerialDriver::class.java)
            }
        }).findAllDrivers(mgr)
        // De-dup by USB device.
        val seen = HashSet<Int>()
        val out = ArrayList<UsbSerialDriver>()
        for (d in drivers) if (seen.add(d.device.deviceId)) out.add(d)
        _state.value = State.Detected(out)
        return out
    }

    suspend fun requestPermission(driver: UsbSerialDriver): Boolean = suspendCancellableCoroutine { cont ->
        val mgr = context.getSystemService(Context.USB_SERVICE) as UsbManager
        if (mgr.hasPermission(driver.device)) {
            cont.resume(true); return@suspendCancellableCoroutine
        }
        val action = ACTION_USB_PERMISSION
        val intent = Intent(action).setPackage(context.packageName)
        val flags = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S)
                        PendingIntent.FLAG_MUTABLE or PendingIntent.FLAG_UPDATE_CURRENT
                    else PendingIntent.FLAG_UPDATE_CURRENT
        val pi = PendingIntent.getBroadcast(context, 0, intent, flags)

        val receiver = object : BroadcastReceiver() {
            override fun onReceive(c: Context?, i: Intent?) {
                if (i?.action != action) return
                val granted = i.getBooleanExtra(UsbManager.EXTRA_PERMISSION_GRANTED, false)
                try { context.unregisterReceiver(this) } catch (_: Exception) {}
                if (cont.isActive) cont.resume(granted)
            }
        }
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            context.registerReceiver(receiver, IntentFilter(action), Context.RECEIVER_NOT_EXPORTED)
        } else {
            @Suppress("UnspecifiedRegisterReceiverFlag")
            context.registerReceiver(receiver, IntentFilter(action))
        }
        mgr.requestPermission(driver.device, pi)
    }

    /**
     * Flash [imageFile] to the chip behind [driver]. Returns true on
     * success, updates [state] for the UI.
     *
     * Caller is responsible for having already obtained USB permission.
     */
    suspend fun flash(driver: UsbSerialDriver, imageFile: File, offset: Int = 0): Boolean = withContext(Dispatchers.IO) {
        val mgr = context.getSystemService(Context.USB_SERVICE) as UsbManager
        val conn = mgr.openDevice(driver.device) ?: run {
            _state.value = State.Error("Could not open USB device")
            return@withContext false
        }
        val port: UsbSerialPort = driver.ports.firstOrNull() ?: run {
            _state.value = State.Error("No serial port on device")
            return@withContext false
        }
        try {
            _state.value = State.Connecting
            port.open(conn)
            // ROM bootloader speaks 115200 8N1.
            port.setParameters(115_200, 8, UsbSerialPort.STOPBITS_1, UsbSerialPort.PARITY_NONE)
            // Pulse RTS/DTR to put the chip into download mode (Heltec
            // CP210x has the standard auto-reset wiring; XIAO native
            // USB-CDC silently no-ops here). Timing stretched because
            // Samsung's USB stack delays DTR/RTS writes by ~40-80ms
            // each, which eats the short pulses that work on Pixel.
            try {
                port.setDTR(false); port.setRTS(true)
                Thread.sleep(200)
                port.setDTR(true);  port.setRTS(false)
                Thread.sleep(100)
                port.setDTR(false); port.setRTS(false)
                Thread.sleep(200)
            } catch (_: Exception) { /* not all drivers support flow control */ }

            val client = EsptoolClient(port)
            _state.value = State.Syncing
            // If sync fails, the chip is not in the ROM bootloader.
            // Recovery path: tap "Enter Flash Mode" on the Devices tab
            // over BLE -- the firmware writes RTC_CNTL_OPTION1 to force
            // download boot, then resets. After that, plugging USB and
            // hitting flash again will sync on the first try.
            if (!client.sync()) {
                _state.value = State.Error(
                    "Sync failed. Tap 'Enter Flash Mode' on the Devices " +
                    "tab over BLE, then retry -- or hold BOOT and tap " +
                    "RESET manually."
                )
                return@withContext false
            }
            val image = imageFile.readBytes()
            client.writeImage(image, offset = offset) { done, total ->
                _state.value = State.Flashing(done, total)
            }
            _state.value = State.Done
            return@withContext true
        } catch (e: Exception) {
            Log.w(TAG, "flash failed", e)
            _state.value = State.Error(e.message ?: "Flash failed")
            return@withContext false
        } finally {
            try { port.close() } catch (_: Exception) {}
        }
    }

    fun reset() { _state.value = State.Idle }

    companion object {
        private const val TAG = "UsbFlasher"
        private const val ACTION_USB_PERMISSION = "com.nexus.mesh.USB_PERMISSION"
    }
}
