package com.nexus.mesh.ble

import android.bluetooth.*
import android.bluetooth.le.*
import android.content.Context
import android.os.ParcelUuid
import android.util.Log
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import java.util.UUID

/**
 * BLE transport -- connects to NEXUS ESP32 devices via Nordic UART Service.
 * Receives LoRa-bridged packets and sends outgoing packets to the radio.
 */
class BleTransport(private val context: Context) {
    companion object {
        private const val TAG = "BleTransport"

        // Nordic UART Service UUIDs
        val NUS_SERVICE_UUID: UUID = UUID.fromString("6e400001-b5a3-f393-e0a9-e50e24dcca9e")
        val NUS_RX_UUID: UUID = UUID.fromString("6e400002-b5a3-f393-e0a9-e50e24dcca9e") // write to device
        val NUS_TX_UUID: UUID = UUID.fromString("6e400003-b5a3-f393-e0a9-e50e24dcca9e") // notify from device
        val CCCD_UUID: UUID = UUID.fromString("00002902-0000-1000-8000-00805f9b34fb")
    }

    data class ScannedDevice(val name: String, val address: String, val rssi: Int)

    interface PacketListener {
        fun onPacketReceived(data: ByteArray)
    }

    private val _devices = MutableStateFlow<List<ScannedDevice>>(emptyList())
    val devices: StateFlow<List<ScannedDevice>> = _devices

    private val _connected = MutableStateFlow(false)
    val connected: StateFlow<Boolean> = _connected

    private val _connectedDevice = MutableStateFlow<String?>(null)
    val connectedDevice: StateFlow<String?> = _connectedDevice

    private var scanner: BluetoothLeScanner? = null
    private var gatt: BluetoothGatt? = null
    private var rxChar: BluetoothGattCharacteristic? = null
    private var listener: PacketListener? = null

    fun setPacketListener(l: PacketListener) { listener = l }

    // --- Scanning ---

    fun startScan() {
        val adapter = BluetoothAdapter.getDefaultAdapter() ?: return
        scanner = adapter.bluetoothLeScanner ?: return

        _devices.value = emptyList()

        val filter = ScanFilter.Builder()
            .setServiceUuid(ParcelUuid(NUS_SERVICE_UUID))
            .build()
        val settings = ScanSettings.Builder()
            .setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY)
            .build()

        scanner?.startScan(listOf(filter), settings, scanCallback)
        Log.i(TAG, "BLE scan started")
    }

    fun stopScan() {
        scanner?.stopScan(scanCallback)
        Log.i(TAG, "BLE scan stopped")
    }

    private val scanCallback = object : ScanCallback() {
        override fun onScanResult(callbackType: Int, result: ScanResult) {
            val name = result.device.name ?: "Unknown"
            val addr = result.device.address
            val rssi = result.rssi

            val existing = _devices.value.toMutableList()
            existing.removeAll { it.address == addr }
            existing.add(ScannedDevice(name, addr, rssi))
            _devices.value = existing
        }
    }

    // --- Connection ---

    fun connect(address: String) {
        val adapter = BluetoothAdapter.getDefaultAdapter() ?: return
        val device = adapter.getRemoteDevice(address)

        stopScan()
        gatt = device.connectGatt(context, false, gattCallback, BluetoothDevice.TRANSPORT_LE)
        Log.i(TAG, "Connecting to $address...")
    }

    fun disconnect() {
        gatt?.disconnect()
        gatt?.close()
        gatt = null
        rxChar = null
        _connected.value = false
        _connectedDevice.value = null
    }

    fun send(data: ByteArray): Boolean {
        val char = rxChar ?: return false
        val g = gatt ?: return false

        // Frame: [LEN_HI][LEN_LO][data]
        val frame = ByteArray(data.size + 2)
        frame[0] = (data.size shr 8).toByte()
        frame[1] = (data.size and 0xFF).toByte()
        System.arraycopy(data, 0, frame, 2, data.size)

        char.value = frame
        char.writeType = BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT
        return g.writeCharacteristic(char)
    }

    private val gattCallback = object : BluetoothGattCallback() {
        override fun onConnectionStateChange(g: BluetoothGatt, status: Int, newState: Int) {
            if (newState == BluetoothProfile.STATE_CONNECTED) {
                Log.i(TAG, "Connected, discovering services...")
                g.requestMtu(517)
            } else if (newState == BluetoothProfile.STATE_DISCONNECTED) {
                Log.i(TAG, "Disconnected")
                _connected.value = false
                _connectedDevice.value = null
                rxChar = null
            }
        }

        override fun onMtuChanged(g: BluetoothGatt, mtu: Int, status: Int) {
            Log.i(TAG, "MTU: $mtu")
            g.discoverServices()
        }

        override fun onServicesDiscovered(g: BluetoothGatt, status: Int) {
            if (status != BluetoothGatt.GATT_SUCCESS) return

            val nus = g.getService(NUS_SERVICE_UUID) ?: run {
                Log.e(TAG, "NUS service not found")
                return
            }

            // RX characteristic (we write to this)
            rxChar = nus.getCharacteristic(NUS_RX_UUID)

            // TX characteristic (device notifies us)
            val txChar = nus.getCharacteristic(NUS_TX_UUID)
            if (txChar != null) {
                g.setCharacteristicNotification(txChar, true)
                val desc = txChar.getDescriptor(CCCD_UUID)
                if (desc != null) {
                    desc.value = BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE
                    g.writeDescriptor(desc)
                }
            }

            _connected.value = true
            _connectedDevice.value = g.device.name ?: g.device.address
            Log.i(TAG, "NUS service ready")
        }

        override fun onCharacteristicChanged(
            g: BluetoothGatt,
            characteristic: BluetoothGattCharacteristic
        ) {
            if (characteristic.uuid == NUS_TX_UUID) {
                val frame = characteristic.value ?: return
                if (frame.size < 3) return

                val pktLen = ((frame[0].toInt() and 0xFF) shl 8) or (frame[1].toInt() and 0xFF)
                if (pktLen + 2 > frame.size) return

                val packet = frame.copyOfRange(2, 2 + pktLen)
                listener?.onPacketReceived(packet)
            }
        }
    }
}
