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
 * Supports config protocol for Meshtastic-style remote settings.
 */
class BleTransport(private val context: Context) {
    companion object {
        private const val TAG = "BleTransport"

        // Nordic UART Service UUIDs
        val NUS_SERVICE_UUID: UUID = UUID.fromString("6e400001-b5a3-f393-e0a9-e50e24dcca9e")
        val NUS_RX_UUID: UUID = UUID.fromString("6e400002-b5a3-f393-e0a9-e50e24dcca9e") // write to device
        val NUS_TX_UUID: UUID = UUID.fromString("6e400003-b5a3-f393-e0a9-e50e24dcca9e") // notify from device
        val CCCD_UUID: UUID = UUID.fromString("00002902-0000-1000-8000-00805f9b34fb")

        // Config protocol magic prefix
        val CFG_MAGIC = byteArrayOf(0xFF.toByte(), 0xFF.toByte(), 0xFF.toByte(), 0xCF.toByte())

        // Config commands
        const val CFG_CMD_GET_CONFIG: Byte = 0x01
        const val CFG_CMD_SET_RADIO: Byte = 0x02
        const val CFG_CMD_SET_SCREEN: Byte = 0x03
        const val CFG_CMD_SET_ROLE: Byte = 0x04
        const val CFG_CMD_REBOOT: Byte = 0x05
        const val CFG_RESP_FLAG: Int = 0x80
    }

    /** Parsed config received from device */
    data class NodeConfig(
        val frequencyHz: Long,
        val bandwidthHz: Long,
        val spreadingFactor: Int,
        val codingRate: Int,
        val txPowerDbm: Int,
        val screenTimeoutMs: Long,
        val nodeRole: Int,
        val nodeAddr: String
    )

    data class ScannedDevice(val name: String, val address: String, val rssi: Int)

    interface PacketListener {
        fun onPacketReceived(data: ByteArray)
    }

    interface ConfigListener {
        fun onConfigReceived(config: NodeConfig)
    }

    private val _devices = MutableStateFlow<List<ScannedDevice>>(emptyList())
    val devices: StateFlow<List<ScannedDevice>> = _devices

    private val _connected = MutableStateFlow(false)
    val connected: StateFlow<Boolean> = _connected

    private val _connectedDevice = MutableStateFlow<String?>(null)
    val connectedDevice: StateFlow<String?> = _connectedDevice

    private val _nodeConfig = MutableStateFlow<NodeConfig?>(null)
    val nodeConfig: StateFlow<NodeConfig?> = _nodeConfig

    private var scanner: BluetoothLeScanner? = null
    private var gatt: BluetoothGatt? = null
    private var rxChar: BluetoothGattCharacteristic? = null
    private var listener: PacketListener? = null
    private var configListener: ConfigListener? = null

    fun setPacketListener(l: PacketListener) { listener = l }
    fun setConfigListener(l: ConfigListener) { configListener = l }

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
        _nodeConfig.value = null
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

    // --- Config Protocol ---

    /** Request current config from device */
    fun requestConfig() {
        val payload = ByteArray(5)
        System.arraycopy(CFG_MAGIC, 0, payload, 0, 4)
        payload[4] = CFG_CMD_GET_CONFIG
        send(payload)
        Log.i(TAG, "Config request sent")
    }

    /** Set radio parameters on device */
    fun setRadioConfig(frequencyHz: Long, bandwidthHz: Long,
                       spreadingFactor: Int, codingRate: Int, txPowerDbm: Int) {
        val payload = ByteArray(16) // magic(4) + cmd(1) + freq(4) + bw(4) + sf(1) + cr(1) + pwr(1)
        System.arraycopy(CFG_MAGIC, 0, payload, 0, 4)
        payload[4] = CFG_CMD_SET_RADIO
        putLeU32(payload, 5, frequencyHz)
        putLeU32(payload, 9, bandwidthHz)
        payload[13] = spreadingFactor.toByte()
        payload[14] = codingRate.toByte()
        payload[15] = txPowerDbm.toByte()
        send(payload)
        Log.i(TAG, "Set radio: freq=$frequencyHz bw=$bandwidthHz sf=$spreadingFactor cr=$codingRate pwr=$txPowerDbm")
    }

    /** Set screen timeout on device */
    fun setScreenTimeout(timeoutMs: Long) {
        val payload = ByteArray(9) // magic(4) + cmd(1) + timeout(4)
        System.arraycopy(CFG_MAGIC, 0, payload, 0, 4)
        payload[4] = CFG_CMD_SET_SCREEN
        putLeU32(payload, 5, timeoutMs)
        send(payload)
        Log.i(TAG, "Set screen timeout: $timeoutMs ms")
    }

    /** Set node role on device */
    fun setNodeRole(role: Int) {
        val payload = ByteArray(6) // magic(4) + cmd(1) + role(1)
        System.arraycopy(CFG_MAGIC, 0, payload, 0, 4)
        payload[4] = CFG_CMD_SET_ROLE
        payload[5] = role.toByte()
        send(payload)
        Log.i(TAG, "Set role: $role")
    }

    /** Reboot the device */
    fun rebootDevice() {
        val payload = ByteArray(5) // magic(4) + cmd(1)
        System.arraycopy(CFG_MAGIC, 0, payload, 0, 4)
        payload[4] = CFG_CMD_REBOOT
        send(payload)
        Log.i(TAG, "Reboot command sent")
    }

    private fun putLeU32(buf: ByteArray, offset: Int, value: Long) {
        buf[offset]     = (value and 0xFF).toByte()
        buf[offset + 1] = ((value shr 8) and 0xFF).toByte()
        buf[offset + 2] = ((value shr 16) and 0xFF).toByte()
        buf[offset + 3] = ((value shr 24) and 0xFF).toByte()
    }

    private fun getLeU32(buf: ByteArray, offset: Int): Long {
        return (buf[offset].toLong() and 0xFF) or
               ((buf[offset + 1].toLong() and 0xFF) shl 8) or
               ((buf[offset + 2].toLong() and 0xFF) shl 16) or
               ((buf[offset + 3].toLong() and 0xFF) shl 24)
    }

    private fun parseConfigResponse(data: ByteArray) {
        // Response after NUS framing stripped: [MAGIC(4)][0x81][freq(4)][bw(4)][sf][cr][pwr][timeout(4)][role][addr(4)] = 25 bytes
        if (data.size < 25) return
        if (data[0] != CFG_MAGIC[0] || data[1] != CFG_MAGIC[1] ||
            data[2] != CFG_MAGIC[2] || data[3] != CFG_MAGIC[3]) return

        val cmd = data[4].toInt() and 0xFF
        if (cmd != (CFG_CMD_GET_CONFIG.toInt() or CFG_RESP_FLAG)) return

        // Offsets into data: [4]=cmd, [5..8]=freq, [9..12]=bw, [13]=sf, [14]=cr, [15]=pwr, [16..19]=timeout, [20]=role, [21..24]=addr
        if (data.size < 25) return

        val config = NodeConfig(
            frequencyHz = getLeU32(data, 5),
            bandwidthHz = getLeU32(data, 9),
            spreadingFactor = data[13].toInt() and 0xFF,
            codingRate = data[14].toInt() and 0xFF,
            txPowerDbm = data[15].toInt(), // signed
            screenTimeoutMs = getLeU32(data, 16),
            nodeRole = data[20].toInt() and 0xFF,
            nodeAddr = String.format("%02X%02X%02X%02X",
                data[21].toInt() and 0xFF,
                data[22].toInt() and 0xFF,
                data[23].toInt() and 0xFF,
                data[24].toInt() and 0xFF)
        )

        _nodeConfig.value = config
        configListener?.onConfigReceived(config)
        Log.i(TAG, "Config received: $config")
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
                _nodeConfig.value = null
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

            // Auto-request config on connect
            android.os.Handler(android.os.Looper.getMainLooper()).postDelayed({
                requestConfig()
            }, 500)
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

                // Check if this is a config response
                if (packet.size >= 5 &&
                    packet[0] == CFG_MAGIC[0] && packet[1] == CFG_MAGIC[1] &&
                    packet[2] == CFG_MAGIC[2] && packet[3] == CFG_MAGIC[3]) {
                    parseConfigResponse(packet)
                } else {
                    listener?.onPacketReceived(packet)
                }
            }
        }
    }
}
