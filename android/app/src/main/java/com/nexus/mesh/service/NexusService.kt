package com.nexus.mesh.service

import android.app.*
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.hardware.usb.UsbManager
import android.net.ConnectivityManager
import android.net.LinkProperties
import android.net.Network
import android.net.NetworkCapabilities
import android.net.NetworkRequest
import android.net.wifi.WifiManager
import android.os.Binder
import android.os.Build
import android.os.IBinder
import android.util.Log
import com.hoho.android.usbserial.driver.UsbSerialProber
import androidx.core.app.NotificationCompat
import androidx.security.crypto.EncryptedSharedPreferences
import androidx.security.crypto.MasterKey
import com.nexus.mesh.R
import com.nexus.mesh.data.*
import com.nexus.mesh.nxm.*
import com.nexus.mesh.ui.MainActivity
import kotlinx.coroutines.*
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow

data class NetworkInterface(
    val type: String,          // "WiFi", "Cellular", "Ethernet", "VPN", "Unknown"
    val name: String?,         // e.g. "wlan0", "rmnet0"
    val addresses: List<String>, // IPv4/IPv6 addresses
    val isMetered: Boolean
)

data class NetworkState(
    val interfaces: List<NetworkInterface> = emptyList(),
    val hasWifi: Boolean = false,
    val hasCellular: Boolean = false,
    val hasEthernet: Boolean = false,
    val hasVpn: Boolean = false,
    val hasAnyInternet: Boolean = false
) {
    val summary: String get() {
        if (interfaces.isEmpty()) return "No network"
        return interfaces.joinToString(" + ") { iface ->
            val addr = iface.addresses.firstOrNull()?.let { " ($it)" } ?: ""
            "${iface.type}$addr"
        }
    }

    val canMulticast: Boolean get() = hasWifi || hasEthernet

    /** True if any active network is unmetered (WiFi/Ethernet without quota). */
    val hasUnmetered: Boolean get() = interfaces.any { !it.isMetered }
}

class NexusService : Service(), NexusNode.Callback {
    companion object {
        private const val TAG = "NexusService"
        private const val CHANNEL_ID = "nexus_mesh"
        private const val NOTIFICATION_ID = 1
        private const val PREFS_NAME = "nexus_identity"            // legacy plaintext prefs (migration source)
        private const val PREFS_NAME_SECURE = "nexus_identity_secure" // EncryptedSharedPreferences
        private const val KEY_IDENTITY = "identity_bytes"
        private const val PREFS_TCP = "nexus_tcp"
        private const val KEY_TCP_ENABLED = "tcp_enabled"
        private const val KEY_TCP_PORT = "tcp_listen_port"
        private const val KEY_TCP_PEERS = "tcp_peers"
        private const val PREFS_NICKNAMES = "nexus_nicknames"
        private const val KEY_MY_NAME = "my_name"
        private const val KEY_NICKNAMES_MIGRATED = "nicknames_migrated_to_room"
        private const val PREFS_PILLARS = "nexus_pillars"
        private const val KEY_PILLAR_LIST = "pillar_list"
        private const val KEY_PILLARS_ENABLED = "pillars_enabled"
        private const val KEY_PILLARS_ALLOW_METERED = "pillars_allow_metered"

        private const val PREFS_ROLE = "nexus_role"
        private const val KEY_ROLE = "node_role"
        const val DEFAULT_ROLE = NexusNode.ROLE_RELAY

        // Mesh tick cadence. The poll handles housekeeping; inbound
        // packets arrive via callbacks on each transport, so a fast
        // tick isn't needed for responsiveness. 500ms cycle = 2Hz.
        private const val POLL_BUDGET_MS   = 50L
        private const val POLL_INTERVAL_MS = 500L
        val DEFAULT_PILLARS = ""
        private const val NETWORK_DEBOUNCE_MS = 1500L

        private const val PREFS_STAMP = "nexus_stamp"
        private const val KEY_STAMP_MIN_DIFFICULTY = "stamp_min_difficulty"
        private const val KEY_STAMP_REJECT = "stamp_reject_under"
        const val DEFAULT_STAMP_MIN_DIFFICULTY = 0   // 0 = accept all, advisory counters only

        private const val PREFS_ANNOUNCE = "nexus_announce"
        private const val KEY_ANNOUNCE_INTERVAL_MS = "announce_interval_ms"
        const val DEFAULT_ANNOUNCE_INTERVAL_MS = 30_000L  // 30 s, matches Reticulum default
    }

    private val node = NexusNode()
    private val binder = LocalBinder()
    private var pollJob: Job? = null
    private val scope = CoroutineScope(Dispatchers.IO + SupervisorJob())
    private var multicastLock: WifiManager.MulticastLock? = null
    private var connectivityManager: ConnectivityManager? = null
    private var networkCallback: ConnectivityManager.NetworkCallback? = null
    private val activeNetworks = mutableMapOf<Network, NetworkCapabilities>()
    private val activeLinkProps = mutableMapOf<Network, LinkProperties>()
    private var networkDebounceJob: Job? = null
    private var udpAutoManaged = true  // true = auto start/stop UDP based on interface type

    // Room DB
    private lateinit var db: NexusDatabase
    lateinit var repository: MessageRepository
        private set

    // Dedup set for inbox-pull requests (once per neighbor per session)
    private val inboxRequested = java.util.concurrent.ConcurrentHashMap.newKeySet<String>()

    /* Peers we've already sent our nickname to in this service session.
     * Keyed by addrHex (uppercase). Reset implicitly on process restart and
     * explicitly when the user changes their own name. */
    private val nicknameSentTo = java.util.concurrent.ConcurrentHashMap.newKeySet<String>()

    // --- Data models (kept for backward compat with UI references) ---

    data class Neighbor(val addr: String, val role: Int)

    data class AnnounceEvent(
        val addr: String,
        val role: Int,
        val firstSeenMs: Long,
        val lastSeenMs: Long,
        val count: Int
    )

    data class RouteInfo(
        val hopCount: Int,
        val viaTransport: Int,
        val nextHop: String,
        val isDirect: Boolean
    )

    data class StampStats(
        val accepted: Long = 0,
        val warned: Long = 0,
        val rejected: Long = 0,
    )

    // --- Observable state ---

    private val _neighbors = MutableStateFlow<List<Neighbor>>(emptyList())
    val neighbors: StateFlow<List<Neighbor>> = _neighbors

    private val _announceStream = MutableStateFlow<List<AnnounceEvent>>(emptyList())
    val announceStream: StateFlow<List<AnnounceEvent>> = _announceStream

    private val _address = MutableStateFlow("--------")
    val address: StateFlow<String> = _address

    private val _tcpActive = MutableStateFlow(false)
    val tcpActive: StateFlow<Boolean> = _tcpActive

    private val _udpActive = MutableStateFlow(false)
    val udpActive: StateFlow<Boolean> = _udpActive

    private val _stampStats = MutableStateFlow(StampStats())
    val stampStats: StateFlow<StampStats> = _stampStats
    private val _stampMinDifficulty = MutableStateFlow(DEFAULT_STAMP_MIN_DIFFICULTY)
    val stampMinDifficulty: StateFlow<Int> = _stampMinDifficulty
    private val _stampReject = MutableStateFlow(false)
    val stampReject: StateFlow<Boolean> = _stampReject

    private val _myName = MutableStateFlow("")
    val myName: StateFlow<String> = _myName

    private val _pillarsEnabled = MutableStateFlow(true)
    val pillarsEnabled: StateFlow<Boolean> = _pillarsEnabled

    private val _pillarList = MutableStateFlow("")
    val pillarList: StateFlow<String> = _pillarList

    private val _pillarConnected = MutableStateFlow(false)
    val pillarConnected: StateFlow<Boolean> = _pillarConnected

    private val _pillarsAllowMetered = MutableStateFlow(false)
    val pillarsAllowMetered: StateFlow<Boolean> = _pillarsAllowMetered

    private val _networkState = MutableStateFlow(NetworkState())
    val networkState: StateFlow<NetworkState> = _networkState

    private val _announceIntervalMs = MutableStateFlow(DEFAULT_ANNOUNCE_INTERVAL_MS)
    val announceIntervalMs: StateFlow<Long> = _announceIntervalMs

    private val _role = MutableStateFlow(DEFAULT_ROLE)
    val role: StateFlow<Int> = _role

    /** Name of the device exposing a LoRa radio over BLE-NUS, if any.
     *  Set by [setBleBridge] from MainActivity when the BLE-bridge connects.
     *  Surfaces in [NetworkState.interfaces] as a "LoRa (BLE)" pseudo-row
     *  so the user can see all bridged transports in one place. */
    @Volatile private var bleBridgeName: String? = null

    /** USB-serial NEXUS adapters currently attached. Maintained by the
     *  USB attach/detach BroadcastReceiver. */
    private val usbDevices = mutableListOf<String>()

    private var announceJob: Job? = null
    private var usbReceiver: BroadcastReceiver? = null

    inner class LocalBinder : Binder() {
        fun getService(): NexusService = this@NexusService
    }

    override fun onBind(intent: Intent?): IBinder = binder

    override fun onCreate() {
        super.onCreate()

        // Init Room DB
        db = NexusDatabase.getInstance(this)
        repository = MessageRepository(db)

        createNotificationChannel()
        startForeground(NOTIFICATION_ID, buildNotification("Starting..."))
        loadStampPrefs()
        startNode()
        registerNetworkCallback()
        registerUsbReceiver()
        refreshUsbDevices()
    }

    override fun onDestroy() {
        unregisterNetworkCallback()
        unregisterUsbReceiver()
        announceJob?.cancel()
        pollJob?.cancel()
        node.stopTcpInet()
        node.stopUdpMulticast()
        multicastLock?.release()
        node.stop()
        scope.cancel()
        super.onDestroy()
    }

    // --- Announce loop (configurable interval + force-announce) ---

    private fun loadAnnouncePrefs() {
        val prefs = getSharedPreferences(PREFS_ANNOUNCE, Context.MODE_PRIVATE)
        _announceIntervalMs.value =
            prefs.getLong(KEY_ANNOUNCE_INTERVAL_MS, DEFAULT_ANNOUNCE_INTERVAL_MS)
    }

    private fun startAnnounceLoop() {
        announceJob?.cancel()
        announceJob = scope.launch {
            // Brief startup grace so transports finish registering before
            // we fire the first announce.
            delay(2_000)
            if (node.isRunning && _announceIntervalMs.value > 0L) node.announce()
            while (isActive) {
                val ms = _announceIntervalMs.value
                if (ms <= 0L) {
                    // "Off" -- poll every minute so a re-enable picks up
                    // without restarting the service.
                    delay(60_000)
                    continue
                }
                delay(ms)
                if (node.isRunning && _announceIntervalMs.value > 0L) {
                    node.announce()
                }
            }
        }
    }

    /** User-facing "Announce now" button hook. */
    fun forceAnnounce() {
        if (node.isRunning) node.announce()
    }

    /** Persist a new auto-announce cadence and restart the loop so the
     *  change takes effect without waiting out the previous interval.
     *  Pass 0 to disable automatic announces (forceAnnounce still works). */
    fun setAnnounceIntervalMs(ms: Long) {
        val clamped = if (ms < 0L) 0L else ms
        _announceIntervalMs.value = clamped
        getSharedPreferences(PREFS_ANNOUNCE, Context.MODE_PRIVATE).edit()
            .putLong(KEY_ANNOUNCE_INTERVAL_MS, clamped).apply()
        startAnnounceLoop()
    }

    // --- Bridged transports surfaced in NetworkState ---

    /** Called by MainActivity whenever the BLE-bridge connection state
     *  changes. Pass the connected device's display name or null when
     *  disconnected. Surfaces as a "LoRa (BLE)" row in the Interfaces card. */
    fun setBleBridge(name: String?) {
        if (bleBridgeName == name) return
        val justConnected = bleBridgeName == null && name != null
        bleBridgeName = name
        rebuildNetworkState()
        /* Rising-edge BLE connect: announce immediately so the firmware
         * (and every LoRa peer it bridges to) sees us without waiting up
         * to a full announce interval. Mirrors the firmware-side
         * rising-edge announce. */
        if (justConnected && node.isRunning) {
            Log.i(TAG, "BLE bridge connected ($name) -- announcing")
            node.announce()
        }
    }

    private fun registerUsbReceiver() {
        val r = object : BroadcastReceiver() {
            override fun onReceive(c: Context, i: Intent) {
                when (i.action) {
                    UsbManager.ACTION_USB_DEVICE_ATTACHED,
                    UsbManager.ACTION_USB_DEVICE_DETACHED -> refreshUsbDevices()
                }
            }
        }
        val filter = IntentFilter().apply {
            addAction(UsbManager.ACTION_USB_DEVICE_ATTACHED)
            addAction(UsbManager.ACTION_USB_DEVICE_DETACHED)
        }
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            registerReceiver(r, filter, Context.RECEIVER_NOT_EXPORTED)
        } else {
            @Suppress("UnspecifiedRegisterReceiverFlag")
            registerReceiver(r, filter)
        }
        usbReceiver = r
        Log.i(TAG, "USB attach/detach receiver registered")
    }

    private fun unregisterUsbReceiver() {
        usbReceiver?.let {
            try { unregisterReceiver(it) } catch (_: IllegalArgumentException) { }
        }
        usbReceiver = null
    }

    private fun refreshUsbDevices() {
        val mgr = getSystemService(Context.USB_SERVICE) as? UsbManager ?: return
        val drivers = try {
            UsbSerialProber.getDefaultProber().findAllDrivers(mgr)
        } catch (e: Exception) {
            Log.w(TAG, "USB enumeration failed: $e")
            emptyList()
        }
        val names = drivers.map { d ->
            d.device.productName ?: "USB ${"%04X:%04X".format(d.device.vendorId, d.device.productId)}"
        }
        synchronized(usbDevices) {
            if (usbDevices == names) return
            usbDevices.clear()
            usbDevices.addAll(names)
        }
        rebuildNetworkState()
        Log.i(TAG, "USB devices: $names")
    }

    // --- Dynamic Network Detection ---

    private fun registerNetworkCallback() {
        connectivityManager = getSystemService(Context.CONNECTIVITY_SERVICE) as? ConnectivityManager
        val cm = connectivityManager ?: return

        val request = NetworkRequest.Builder()
            .addCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET)
            .build()

        networkCallback = object : ConnectivityManager.NetworkCallback() {
            override fun onAvailable(network: Network) {
                Log.i(TAG, "Network available: $network")
                scheduleNetworkUpdate()
            }

            override fun onLost(network: Network) {
                synchronized(activeNetworks) {
                    activeNetworks.remove(network)
                    activeLinkProps.remove(network)
                }
                Log.i(TAG, "Network lost: $network (${activeNetworks.size} remaining)")
                if (synchronized(activeNetworks) { activeNetworks.isEmpty() }) {
                    onNetworkLost()
                } else {
                    rebuildNetworkState()
                    scheduleNetworkUpdate()
                }
            }

            override fun onCapabilitiesChanged(network: Network, caps: NetworkCapabilities) {
                synchronized(activeNetworks) { activeNetworks[network] = caps }
                rebuildNetworkState()
                scheduleNetworkUpdate()
            }

            override fun onLinkPropertiesChanged(network: Network, lp: LinkProperties) {
                synchronized(activeNetworks) { activeLinkProps[network] = lp }
                rebuildNetworkState()
                Log.d(TAG, "Link properties: iface=${lp.interfaceName} " +
                        "addrs=${lp.linkAddresses.map { it.address.hostAddress }}")
            }
        }

        cm.registerNetworkCallback(request, networkCallback!!)
        Log.i(TAG, "Network callback registered")
    }

    private fun unregisterNetworkCallback() {
        networkCallback?.let { cb ->
            connectivityManager?.unregisterNetworkCallback(cb)
            Log.i(TAG, "Network callback unregistered")
        }
        networkCallback = null
        connectivityManager = null
    }

    private fun rebuildNetworkState() {
        val interfaces = mutableListOf<NetworkInterface>()
        var hasWifi = false
        var hasCellular = false
        var hasEthernet = false
        var hasVpn = false

        synchronized(activeNetworks) {
            for ((network, caps) in activeNetworks) {
                val lp = activeLinkProps[network]
                val type = when {
                    caps.hasTransport(NetworkCapabilities.TRANSPORT_WIFI) -> { hasWifi = true; "WiFi" }
                    caps.hasTransport(NetworkCapabilities.TRANSPORT_CELLULAR) -> { hasCellular = true; "Cellular" }
                    caps.hasTransport(NetworkCapabilities.TRANSPORT_ETHERNET) -> { hasEthernet = true; "Ethernet" }
                    caps.hasTransport(NetworkCapabilities.TRANSPORT_VPN) -> { hasVpn = true; "VPN" }
                    else -> "Unknown"
                }
                val addrs = lp?.linkAddresses
                    ?.mapNotNull { it.address.hostAddress }
                    ?.filter { !it.contains(":") }  // IPv4 only for display
                    ?: emptyList()
                val isMetered = !caps.hasCapability(NetworkCapabilities.NET_CAPABILITY_NOT_METERED)

                interfaces.add(NetworkInterface(
                    type = type,
                    name = lp?.interfaceName,
                    addresses = addrs,
                    isMetered = isMetered
                ))
            }
        }

        // hasAnyInternet must be computed from IP interfaces only -- the
        // BLE-bridged LoRa and USB serial entries below are local
        // transports, not internet uplinks.
        val hasIp = interfaces.isNotEmpty()

        // Bridged / local transports the user should be aware of:
        //   - LoRa over the BLE-connected ESP32 (acts as the phone's
        //     "LoRa radio").
        //   - USB-serial NEXUS adapters plugged into the phone.
        // These are surfaced as additional rows in the Interfaces card
        // so the user can see every active transport in one place.
        bleBridgeName?.let { name ->
            interfaces.add(NetworkInterface(
                type = "LoRa (BLE)",
                name = name,
                addresses = listOf("via BLE bridge"),
                isMetered = false
            ))
        }
        synchronized(usbDevices) {
            for (devName in usbDevices) {
                interfaces.add(NetworkInterface(
                    type = "USB Serial",
                    name = devName,
                    addresses = listOf("attached"),
                    isMetered = false
                ))
            }
        }

        val newState = NetworkState(
            interfaces = interfaces,
            hasWifi = hasWifi,
            hasCellular = hasCellular,
            hasEthernet = hasEthernet,
            hasVpn = hasVpn,
            hasAnyInternet = hasIp
        )

        val prev = _networkState.value
        _networkState.value = newState

        // Log meaningful transitions
        if (prev.hasWifi != newState.hasWifi || prev.hasCellular != newState.hasCellular ||
            prev.hasEthernet != newState.hasEthernet || prev.hasVpn != newState.hasVpn) {
            Log.i(TAG, "Network state: ${newState.summary}")
        }
    }

    private fun scheduleNetworkUpdate() {
        // Cancel any pending debounce -- only the latest change fires
        networkDebounceJob?.cancel()
        networkDebounceJob = scope.launch {
            delay(NETWORK_DEBOUNCE_MS)
            onNetworkChanged()
        }
    }

    private fun onNetworkChanged() {
        val state = _networkState.value
        Log.i(TAG, "Network change handler: ${state.summary}")

        // --- UDP multicast: auto-manage based on interface type ---
        if (udpAutoManaged) {
            if (state.canMulticast && !_udpActive.value) {
                Log.i(TAG, "WiFi/Ethernet detected: starting UDP multicast")
                startUdpMulticast()
            } else if (!state.canMulticast && _udpActive.value) {
                Log.i(TAG, "No WiFi/Ethernet: stopping UDP multicast (cellular only)")
                node.stopUdpMulticast()
                multicastLock?.release()
                _udpActive.value = false
            } else if (state.canMulticast && _udpActive.value) {
                // WiFi still up but interface may have changed (e.g. SSID switch)
                Log.i(TAG, "Network change: restarting UDP multicast for new interfaces")
                node.stopUdpMulticast()
                multicastLock?.release()
                startUdpMulticast()
            }
        }

        // --- Pillar metered policy: disconnect on metered-only when disallowed ---
        val pillarsEnabled = _pillarsEnabled.value
        val pillarList = _pillarList.value
        val allowMetered = _pillarsAllowMetered.value
        val onlyMetered = state.hasAnyInternet && !state.hasUnmetered

        if (_pillarConnected.value && pillarsEnabled && !allowMetered && onlyMetered) {
            Log.i(TAG, "Network change: dropped to metered-only, stopping pillars")
            node.stopTcpInet()
            _tcpActive.value = false
            _pillarConnected.value = false
        }

        // --- Reconnect pillars if we have internet but lost connection ---
        if (state.hasAnyInternet) {
            if (pillarsEnabled && pillarList.isNotBlank() && !_pillarConnected.value) {
                Log.i(TAG, "Network change: reconnecting pillars")
                connectToPillars()
            }

            // Restart manual TCP if it was active but socket died
            val tcpPrefs = getSharedPreferences(PREFS_TCP, Context.MODE_PRIVATE)
            if (tcpPrefs.getBoolean(KEY_TCP_ENABLED, false) && !_tcpActive.value) {
                val port = tcpPrefs.getInt(KEY_TCP_PORT, 4242)
                val peers = tcpPrefs.getString(KEY_TCP_PEERS, "") ?: ""
                Log.i(TAG, "Network change: restarting TCP inet")
                startTcpInetFromConfig(port, peers)
            }
        }

        updateTransportNotification()
    }

    private fun onNetworkLost() {
        Log.w(TAG, "All networks lost")
        _networkState.value = NetworkState()
        _udpActive.value = false
        _pillarConnected.value = false
        updateTransportNotification()
    }

    private fun getEncryptedPrefs(): android.content.SharedPreferences {
        val masterKey = MasterKey.Builder(this)
            .setKeyScheme(MasterKey.KeyScheme.AES256_GCM)
            .build()
        return EncryptedSharedPreferences.create(
            this,
            PREFS_NAME_SECURE,
            masterKey,
            EncryptedSharedPreferences.PrefKeyEncryptionScheme.AES256_SIV,
            EncryptedSharedPreferences.PrefValueEncryptionScheme.AES256_GCM
        )
    }

    private fun startNode() {
        val encPrefs = getEncryptedPrefs()

        // One-time migration from old plaintext SharedPreferences
        val oldPrefs = getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)
        val oldEncoded = oldPrefs.getString(KEY_IDENTITY, null)
        if (oldEncoded != null && encPrefs.getString(KEY_IDENTITY, null) == null) {
            Log.i(TAG, "Migrating identity from plaintext prefs to EncryptedSharedPreferences")
            encPrefs.edit().putString(KEY_IDENTITY, oldEncoded).apply()
            oldPrefs.edit().remove(KEY_IDENTITY).apply()
        }

        val savedBytes = encPrefs.getString(KEY_IDENTITY, null)

        loadRole()
        val startRole = _role.value

        val ok = if (savedBytes != null) {
            val bytes = android.util.Base64.decode(savedBytes, android.util.Base64.DEFAULT)
            node.initWithIdentity(startRole, bytes, this)
        } else {
            node.init(startRole, this)
        }

        if (!ok) {
            Log.e(TAG, "Failed to start node")
            stopSelf()
            return
        }

        node.getIdentityBytes()?.let { bytes ->
            val encoded = android.util.Base64.encodeToString(bytes, android.util.Base64.DEFAULT)
            encPrefs.edit().putString(KEY_IDENTITY, encoded).apply()
        }

        _address.value = node.getAddressHex()
        loadMyName()
        migrateNicknames()
        updateNotification("Node: ${_address.value}")

        // Mesh tick runs at 2 Hz idle (was 10 Hz). The poll handles
        // timer-driven housekeeping (dedup expiry, fragment timeouts,
        // outbound queue drain). Inbound packets arrive via callback
        // on BLE/TCP/UDP transports, so they don't need a fast poll.
        // Cutting from ~100 ms cycle to ~500 ms is the single largest
        // battery win in the foreground service.
        pollJob = scope.launch {
            while (isActive) {
                node.poll(POLL_BUDGET_MS.toInt())
                delay(POLL_INTERVAL_MS - POLL_BUDGET_MS)
            }
        }

        loadAnnouncePrefs()
        startAnnounceLoop()

        val tcpPrefs = getSharedPreferences(PREFS_TCP, Context.MODE_PRIVATE)
        if (tcpPrefs.getBoolean(KEY_TCP_ENABLED, false)) {
            val port = tcpPrefs.getInt(KEY_TCP_PORT, 4242)
            val peersStr = tcpPrefs.getString(KEY_TCP_PEERS, "") ?: ""
            startTcpInetFromConfig(port, peersStr)
        }

        // UDP multicast will auto-start when network callback detects WiFi/Ethernet.
        // Attempt now in case connectivity is already available (callback may fire before poll).
        val cm = getSystemService(Context.CONNECTIVITY_SERVICE) as? ConnectivityManager
        val activeCaps = cm?.getNetworkCapabilities(cm.activeNetwork)
        val hasLocalNet = activeCaps?.hasTransport(NetworkCapabilities.TRANSPORT_WIFI) == true ||
                          activeCaps?.hasTransport(NetworkCapabilities.TRANSPORT_ETHERNET) == true
        if (hasLocalNet) {
            startUdpMulticast()
        } else {
            Log.i(TAG, "No WiFi/Ethernet at startup, UDP multicast deferred to network callback")
        }

        connectToPillars()

        Log.i(TAG, "Node started: ${_address.value}")
    }

    private fun migrateNicknames() {
        val prefs = getSharedPreferences(PREFS_NICKNAMES, Context.MODE_PRIVATE)
        if (prefs.getBoolean(KEY_NICKNAMES_MIGRATED, false)) return

        scope.launch {
            val all = prefs.all
            for ((key, value) in all) {
                if (key != KEY_MY_NAME && key != KEY_NICKNAMES_MIGRATED && value is String) {
                    repository.setNickname(key, value)
                }
            }
            prefs.edit().putBoolean(KEY_NICKNAMES_MIGRATED, true).apply()
            Log.i(TAG, "Migrated ${all.size} nicknames to Room")
        }
    }

    private fun connectToPillars() {
        val prefs = getSharedPreferences(PREFS_PILLARS, Context.MODE_PRIVATE)
        val enabled = prefs.getBoolean(KEY_PILLARS_ENABLED, true)
        _pillarsEnabled.value = enabled
        val savedPillars = prefs.getString(KEY_PILLAR_LIST, DEFAULT_PILLARS) ?: ""
        _pillarList.value = savedPillars
        val allowMetered = prefs.getBoolean(KEY_PILLARS_ALLOW_METERED, false)
        _pillarsAllowMetered.value = allowMetered

        if (!enabled || savedPillars.isBlank()) {
            if (savedPillars.isBlank()) {
                Log.i(TAG, "No Pillar configured -- node will be isolated until UDP/manual TCP discovery succeeds")
            } else {
                Log.i(TAG, "Pillars: disabled")
            }
            return
        }

        if (!allowMetered && !_networkState.value.hasUnmetered &&
                _networkState.value.hasAnyInternet) {
            Log.i(TAG, "Pillars: skipping auto-connect (metered network and policy disallows)")
            _pillarConnected.value = false
            return
        }

        if (_tcpActive.value) {
            Log.i(TAG, "Pillars: TCP already active, skipping auto-connect")
            // Only mark pillar-connected if there are actual pillar hosts configured
            _pillarConnected.value = parsePillarHosts(savedPillars).isNotEmpty()
            return
        }

        val pillarHosts = parsePillarHosts(savedPillars)
        val ok = node.startTcpInet(0, pillarHosts, parsePillarPorts(savedPillars))
        // Only set pillarConnected if we actually dialed at least one pillar host
        _pillarConnected.value = ok && pillarHosts.isNotEmpty()
        _tcpActive.value = ok
        if (ok) {
            Log.i(TAG, "Pillars: connected to ${countPillars(savedPillars)} pillar(s)")
            updateTransportNotification()
        } else {
            Log.w(TAG, "Pillars: failed to connect")
        }
    }

    private fun parsePillarHosts(pillars: String): Array<String> {
        return pillars.split(",").mapNotNull { p ->
            val parts = p.trim().split(":")
            if (parts.size == 2 && parts[1].toIntOrNull() != null) parts[0].trim()
            else null
        }.toTypedArray()
    }

    private fun parsePillarPorts(pillars: String): IntArray {
        return pillars.split(",").mapNotNull { p ->
            val parts = p.trim().split(":")
            if (parts.size == 2) parts[1].trim().toIntOrNull()
            else null
        }.toIntArray()
    }

    private fun countPillars(pillars: String): Int {
        return pillars.split(",").count { it.trim().contains(":") }
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

    // --- Proof-of-Work stamp enforcement ---

    private fun loadStampPrefs() {
        val prefs = getSharedPreferences(PREFS_STAMP, Context.MODE_PRIVATE)
        _stampMinDifficulty.value = prefs.getInt(
            KEY_STAMP_MIN_DIFFICULTY, DEFAULT_STAMP_MIN_DIFFICULTY
        ).coerceIn(0, 32)
        _stampReject.value = prefs.getBoolean(KEY_STAMP_REJECT, false)
    }

    fun setStampMinDifficulty(bits: Int) {
        val clamped = bits.coerceIn(0, 32)
        _stampMinDifficulty.value = clamped
        getSharedPreferences(PREFS_STAMP, Context.MODE_PRIVATE)
            .edit().putInt(KEY_STAMP_MIN_DIFFICULTY, clamped).apply()
    }

    fun setStampReject(enabled: Boolean) {
        _stampReject.value = enabled
        getSharedPreferences(PREFS_STAMP, Context.MODE_PRIVATE)
            .edit().putBoolean(KEY_STAMP_REJECT, enabled).apply()
    }

    fun resetStampStats() { _stampStats.value = StampStats() }

    /**
     * Check the PoW stamp on a serialized NXM buffer against the user's
     * minimum difficulty. Updates live counters and returns true if the
     * caller should continue processing the message, or false if the
     * "reject under difficulty" policy is on and the stamp failed.
     */
    private fun verifyInboundStamp(buf: ByteArray, srcHex: String): Boolean {
        val minBits = _stampMinDifficulty.value
        if (minBits <= 0) {
            // Advisory mode: still count, but don't reject.
            when (NexusNode.nativeVerifyStamp(buf, 1)) {
                NexusNode.STAMP_OK -> _stampStats.value =
                    _stampStats.value.copy(accepted = _stampStats.value.accepted + 1)
                else -> { /* no stamp; stay silent */ }
            }
            return true
        }
        val result = NexusNode.nativeVerifyStamp(buf, minBits)
        return when (result) {
            NexusNode.STAMP_OK -> {
                _stampStats.value = _stampStats.value.copy(
                    accepted = _stampStats.value.accepted + 1
                )
                true
            }
            NexusNode.STAMP_UNDER_DIFFICULTY -> {
                if (_stampReject.value) {
                    _stampStats.value = _stampStats.value.copy(
                        rejected = _stampStats.value.rejected + 1
                    )
                    Log.w(TAG, "Reject inbound from $srcHex: stamp below ${minBits} bits")
                    false
                } else {
                    _stampStats.value = _stampStats.value.copy(
                        warned = _stampStats.value.warned + 1
                    )
                    Log.w(TAG, "Warn inbound from $srcHex: stamp below ${minBits} bits")
                    true
                }
            }
            else -> {  // STAMP_INVALID or error: forged or malformed stamp -> always reject
                _stampStats.value = _stampStats.value.copy(
                    rejected = _stampStats.value.rejected + 1
                )
                Log.w(TAG, "Reject inbound from $srcHex: invalid stamp (err=$result)")
                false
            }
        }
    }

    // --- NexusNode.Callback ---

    override fun onGroup(groupId: ByteArray, src: ByteArray, data: ByteArray) {
        val groupIdHex = groupId.joinToString("") { "%02X".format(it) }
        val srcHex = src.joinToString("") { "%02X".format(it) }
        Log.i(TAG, "Group msg in $groupIdHex from $srcHex: ${data.size} bytes")

        // Try to parse as NXM
        if (NxmParser.isNxm(data)) {
            if (data.size >= 3 && (data[2].toInt() and NxmFlag.STAMPED) != 0) {
                if (!verifyInboundStamp(data, srcHex)) return
            }
            val nxm = NxmParser.parse(data)
            if (nxm != null) {
                val text = nxm.text ?: return
                scope.launch {
                    repository.ensureGroup(groupIdHex)
                    repository.insertGroupMessage(MessageEntity(
                        peerAddr = srcHex,
                        text = text,
                        timestamp = System.currentTimeMillis(),
                        isOutgoing = false,
                        isDirect = false,
                        nxmMsgId = nxm.msgIdHex,
                        messageType = MessageType.TEXT,
                        groupId = groupIdHex
                    ))
                }
                showMessageNotification("Group $groupIdHex", "$srcHex: $text")
                return
            }
        }

        // Fallback: raw text
        val text = String(data, Charsets.UTF_8)
        scope.launch {
            repository.ensureGroup(groupIdHex)
            repository.insertGroupMessage(MessageEntity(
                peerAddr = srcHex,
                text = text,
                timestamp = System.currentTimeMillis(),
                isOutgoing = false,
                isDirect = false,
                groupId = groupIdHex
            ))
        }
        showMessageNotification("Group $groupIdHex", "$srcHex: $text")
    }

    override fun onData(src: ByteArray, data: ByteArray) {
        val srcHex = src.joinToString("") { "%02X".format(it) }
        Log.i(TAG, "Data from $srcHex: ${data.size} bytes")

        val isDirect = node.isNeighbor(src) >= 0

        // sendMessage() always wraps the text in NXM (TLV-encoded
        // structured envelope) and tries sendSession first, falling
        // back to raw send. So inbound raw frames are usually NXM too
        // and must be parsed -- otherwise the receiver renders the
        // raw TLV bytes as UTF-8 garbage. Mirror onSession's logic.
        if (NxmParser.isNxm(data)) {
            val nxm = NxmParser.parse(data)
            if (nxm != null) {
                handleNxmMessage(srcHex, src, nxm, isDirect)
                return
            }
        }

        // Fallback: legacy plain-UTF-8 senders (kept so older apps stay
        // interoperable until the network is fully on NXM).
        val text = String(data, Charsets.UTF_8)
        scope.launch {
            repository.insertMessage(MessageEntity(
                peerAddr = srcHex,
                text = text,
                timestamp = System.currentTimeMillis(),
                isOutgoing = false,
                isDirect = isDirect
            ))
        }
        showMessageNotification(srcHex, text)
    }

    override fun onNeighbor(addr: ByteArray, role: Int) {
        val addrHex = addr.joinToString("") { "%02X".format(it) }
        Log.i(TAG, "Neighbor: $addrHex role=$role")

        val existing = _neighbors.value.toMutableList()
        existing.removeAll { it.addr == addrHex }
        existing.add(Neighbor(addrHex, role))
        _neighbors.value = existing

        // Bump lastSeen + refresh role on known contacts (don't auto-create).
        scope.launch {
            repository.getContact(addrHex)?.let {
                repository.upsertContact(it.copy(
                    lastSeen = System.currentTimeMillis(),
                    role = role
                ))
            }
        }

        // Announce stream: keep most-recent 200 events, coalesce by addr.
        val nowMs = System.currentTimeMillis()
        val stream = _announceStream.value.toMutableList()
        val existingIdx = stream.indexOfFirst { it.addr == addrHex }
        if (existingIdx >= 0) {
            val prior = stream.removeAt(existingIdx)
            stream.add(0, prior.copy(role = role, lastSeenMs = nowMs, count = prior.count + 1))
        } else {
            stream.add(0, AnnounceEvent(addrHex, role, nowMs, nowMs, 1))
        }
        if (stream.size > 200) stream.subList(200, stream.size).clear()
        _announceStream.value = stream

        // Pull any stored-and-forward packets held for us by pillars/vaults/anchors.
        // Request once per neighbor per session.
        if (role >= NexusNode.ROLE_ANCHOR && inboxRequested.add(addrHex)) {
            if (node.requestInbox(addr)) {
                Log.i(TAG, "Inbox pull requested from $addrHex (role=$role)")
            }
        }

        // Greet non-infrastructure peers with our nickname so they can show
        // a friendly name instead of our 4-byte address. Skip pillars/vaults
        // (they don't have a UI) and skip if we have no name set.
        if (role < NexusNode.ROLE_PILLAR) {
            sendNicknameIfNeeded(addr, addrHex)
        }
    }

    /** Send a NICKNAME NXM to one peer, once per service session.
     *  Falls back to raw send when no session is established yet so a
     *  fresh peer still learns our name on first contact. */
    private fun sendNicknameIfNeeded(addr: ByteArray, addrHex: String) {
        val name = _myName.value
        if (name.isBlank()) return
        if (!nicknameSentTo.add(addrHex)) return
        val nxm = NxmBuilder.buildNickname(name)
        scope.launch {
            val ok = node.sendSession(addr, nxm) || node.send(addr, nxm)
            if (!ok) nicknameSentTo.remove(addrHex)  // retry next announce
        }
    }

    override fun onSession(src: ByteArray, data: ByteArray) {
        val srcHex = src.joinToString("") { "%02X".format(it) }
        Log.i(TAG, "Session msg from $srcHex: ${data.size} bytes")

        val isDirect = node.isNeighbor(src) >= 0

        // Try to parse as NXM
        if (NxmParser.isNxm(data)) {
            if (data.size >= 3 && (data[2].toInt() and NxmFlag.STAMPED) != 0) {
                if (!verifyInboundStamp(data, srcHex)) return
            }
            val nxm = NxmParser.parse(data)
            if (nxm != null) {
                handleNxmMessage(srcHex, src, nxm, isDirect)
                return
            }
        }

        // Fallback: treat as raw text for backward compat
        val text = String(data, Charsets.UTF_8)
        scope.launch {
            repository.insertMessage(MessageEntity(
                peerAddr = srcHex,
                text = text,
                timestamp = System.currentTimeMillis(),
                isOutgoing = false,
                isDirect = isDirect
            ))
        }
        showMessageNotification(srcHex, text)
    }

    private fun handleNxmMessage(srcHex: String, src: ByteArray,
                                  nxm: NxmMessage, isDirect: Boolean) {
        when (nxm.type) {
            NxmType.TEXT -> {
                val text = nxm.text ?: return
                val msgIdHex = nxm.msgIdHex
                scope.launch {
                    repository.insertMessage(MessageEntity(
                        peerAddr = srcHex,
                        text = text,
                        timestamp = System.currentTimeMillis(),
                        isOutgoing = false,
                        isDirect = isDirect,
                        nxmMsgId = msgIdHex,
                        messageType = MessageType.TEXT
                    ))
                    // Auto-send ACK back. Fall back to raw send when no
                    // session exists yet (the inbound TEXT may have arrived
                    // via the unencrypted fallback path); otherwise the
                    // sender's delivery-tick never updates.
                    val msgId = nxm.msgId
                    if (msgId != null) {
                        val ack = NxmBuilder.buildAck(msgId)
                        if (!node.sendSession(src, ack)) {
                            node.send(src, ack)
                        }
                    }
                }
                showMessageNotification(srcHex, text)
            }
            NxmType.ACK -> {
                // Update delivery status to DELIVERED
                val msgIdHex = nxm.msgIdHex ?: return
                scope.launch {
                    repository.updateDeliveryStatus(msgIdHex, DeliveryStatus.DELIVERED)
                }
                Log.i(TAG, "ACK received for $msgIdHex")
            }
            NxmType.READ -> {
                // Update delivery status to READ
                val msgIdHex = nxm.msgIdHex ?: return
                scope.launch {
                    repository.updateDeliveryStatus(msgIdHex, DeliveryStatus.READ)
                }
                Log.i(TAG, "READ receipt for $msgIdHex")
            }
            NxmType.LOCATION -> {
                val latField = nxm.findField(NxmFieldType.LATITUDE)
                val lonField = nxm.findField(NxmFieldType.LONGITUDE)
                if (latField != null && lonField != null) {
                    val lat = java.nio.ByteBuffer.wrap(latField.data)
                        .order(java.nio.ByteOrder.LITTLE_ENDIAN).getInt() / 1e7
                    val lon = java.nio.ByteBuffer.wrap(lonField.data)
                        .order(java.nio.ByteOrder.LITTLE_ENDIAN).getInt() / 1e7
                    scope.launch {
                        repository.insertMessage(MessageEntity(
                            peerAddr = srcHex,
                            text = "Location: %.5f, %.5f".format(lat, lon),
                            timestamp = System.currentTimeMillis(),
                            isOutgoing = false,
                            isDirect = isDirect,
                            nxmMsgId = nxm.msgIdHex,
                            messageType = MessageType.LOCATION,
                            latitude = lat,
                            longitude = lon
                        ))
                    }
                    showMessageNotification(srcHex, "Shared a location")
                }
            }
            NxmType.IMAGE, NxmType.FILE -> {
                val fname = nxm.filename ?: "file"
                val fdata = nxm.filedata
                val msgType = if (nxm.type == NxmType.IMAGE) MessageType.IMAGE else MessageType.FILE
                scope.launch {
                    var mediaPath: String? = null
                    if (fdata != null) {
                        val dir = getExternalFilesDir("nexus_media")
                        if (dir != null) {
                            dir.mkdirs()
                            val f = java.io.File(dir, "${System.currentTimeMillis()}_$fname")
                            f.writeBytes(fdata)
                            mediaPath = f.absolutePath
                        }
                    }
                    repository.insertMessage(MessageEntity(
                        peerAddr = srcHex,
                        text = if (nxm.type == NxmType.IMAGE) "Image: $fname" else "File: $fname",
                        timestamp = System.currentTimeMillis(),
                        isOutgoing = false,
                        isDirect = isDirect,
                        nxmMsgId = nxm.msgIdHex,
                        messageType = msgType,
                        mediaPath = mediaPath,
                        fileName = fname,
                        mimeType = nxm.mimetype
                    ))
                }
                showMessageNotification(srcHex, if (nxm.type == NxmType.IMAGE) "Sent an image" else "Sent a file")
            }
            NxmType.VOICE_NOTE -> {
                val fdata = nxm.filedata
                val durField = nxm.findField(NxmFieldType.DURATION)
                val durSec = if (durField != null && durField.data.size >= 2) {
                    java.nio.ByteBuffer.wrap(durField.data).order(java.nio.ByteOrder.LITTLE_ENDIAN).getShort().toInt()
                } else 0
                scope.launch {
                    var mediaPath: String? = null
                    if (fdata != null) {
                        val dir = getExternalFilesDir("nexus_media")
                        if (dir != null) {
                            dir.mkdirs()
                            val f = java.io.File(dir, "${System.currentTimeMillis()}_voice")
                            f.writeBytes(fdata)
                            mediaPath = f.absolutePath
                        }
                    }
                    repository.insertMessage(MessageEntity(
                        peerAddr = srcHex,
                        text = "Voice note (${durSec}s)",
                        timestamp = System.currentTimeMillis(),
                        isOutgoing = false,
                        isDirect = isDirect,
                        nxmMsgId = nxm.msgIdHex,
                        messageType = MessageType.VOICE_NOTE,
                        mediaPath = mediaPath,
                        duration = durSec
                    ))
                }
                showMessageNotification(srcHex, "Sent a voice note")
            }
            NxmType.NICKNAME -> {
                val name = nxm.nickname
                if (name != null) {
                    scope.launch {
                        repository.setNickname(srcHex, name)
                    }
                }
            }
            else -> {
                // Unhandled NXM type -- log and ignore
                Log.w(TAG, "Unhandled NXM type: ${nxm.type}")
            }
        }
    }

    // --- Public API: Messages ---

    fun getMessages(peerAddr: String): Flow<List<MessageEntity>> =
        repository.getMessages(peerAddr)

    fun getConversations(): Flow<List<ConversationEntity>> =
        repository.getConversations()

    /**
     * Export the current identity as a passphrase-encrypted Base64 blob.
     * Returns null if the node isn't running yet.
     */
    fun exportIdentity(passphrase: CharArray): String? {
        val bytes = node.getIdentityBytes() ?: return null
        return try {
            IdentityBackup.encrypt(bytes, passphrase)
        } finally {
            passphrase.fill('\u0000')
        }
    }

    /**
     * Import an identity backup, replacing the current identity.
     * Stops the node, overwrites stored identity, restarts the node.
     * Returns true on success. Throws IdentityBackup.BadBackupException /
     * BadPassphraseException on bad input.
     */
    fun importIdentity(blob: String, passphrase: CharArray): Boolean {
        val plain = try {
            IdentityBackup.decrypt(blob, passphrase)
        } finally {
            passphrase.fill('\u0000')
        }

        pollJob?.cancel()
        pollJob = null
        node.stop()

        val encoded = android.util.Base64.encodeToString(plain, android.util.Base64.DEFAULT)
        getEncryptedPrefs().edit().putString(KEY_IDENTITY, encoded).apply()
        plain.fill(0)

        startNode()
        return node.isRunning
    }

    /**
     * Send READ receipts for every incoming message from [peerAddr] that hasn't
     * already been receipted, then mark them READ locally so we don't resend.
     * Called when the user opens a conversation.
     */
    fun sendReadReceipts(peerAddr: String) {
        val destBytes = hexToBytes(peerAddr) ?: return
        scope.launch {
            val pending = repository.getUnreadIncomingForPeer(peerAddr)
            if (pending.isEmpty()) return@launch
            for (m in pending) {
                val hex = m.nxmMsgId ?: continue
                val msgId = hexToBytes(hex) ?: continue
                val nxm = NxmBuilder.buildRead(msgId)
                node.sendSession(destBytes, nxm) || node.send(destBytes, nxm)
            }
            repository.markIncomingRead(peerAddr)
            repository.clearUnread(peerAddr)
        }
    }

    fun sendMessage(dest: String, text: String): Boolean {
        val destBytes = hexToBytes(dest) ?: return false

        // Build NXM TEXT message
        val msgId = NxmBuilder.generateMsgId()
        val nxmData = NxmBuilder.buildText(text, msgId = msgId)
        val msgIdHex = msgId.joinToString("") { "%02X".format(it) }

        // Send via session (encrypted) first, fallback to raw
        val ok = node.sendSession(destBytes, nxmData) || node.send(destBytes, nxmData)
        if (ok) {
            val isDirect = node.isNeighbor(destBytes) >= 0
            scope.launch {
                repository.insertMessage(MessageEntity(
                    peerAddr = dest,
                    text = text,
                    timestamp = System.currentTimeMillis(),
                    isOutgoing = true,
                    isDirect = isDirect,
                    deliveryStatus = DeliveryStatus.SENT,
                    nxmMsgId = msgIdHex,
                    messageType = MessageType.TEXT
                ))
            }
        }
        return ok
    }

    /**
     * Send an image. [data] is the full encoded JPEG/PNG bytes — caller is
     * responsible for downscaling before calling (max NX_FRAG_MAX_MESSAGE
     * minus NXM envelope, ~3.5 kB of attachment in practice).
     */
    fun sendImage(dest: String, filename: String, data: ByteArray,
                  thumbnail: ByteArray? = null): Boolean {
        val destBytes = hexToBytes(dest) ?: return false
        val nxmData = NxmBuilder.buildImage(filename, data, thumbnail)
        val ok = node.sendLarge(destBytes, nxmData)
        if (ok) {
            scope.launch {
                val dir = getExternalFilesDir("nexus_media")
                var mediaPath: String? = null
                if (dir != null) {
                    dir.mkdirs()
                    val f = java.io.File(dir, "${System.currentTimeMillis()}_$filename")
                    f.writeBytes(data)
                    mediaPath = f.absolutePath
                }
                repository.insertMessage(MessageEntity(
                    peerAddr = dest,
                    text = "Image: $filename",
                    timestamp = System.currentTimeMillis(),
                    isOutgoing = true,
                    isDirect = node.isNeighbor(destBytes) >= 0,
                    deliveryStatus = DeliveryStatus.SENT,
                    messageType = MessageType.IMAGE,
                    mediaPath = mediaPath,
                    fileName = filename,
                    mimeType = "image/jpeg"
                ))
            }
        }
        return ok
    }

    fun sendFile(dest: String, filename: String, mimetype: String,
                 data: ByteArray): Boolean {
        val destBytes = hexToBytes(dest) ?: return false
        val nxmData = NxmBuilder.buildFile(filename, mimetype, data)
        val ok = node.sendLarge(destBytes, nxmData)
        if (ok) {
            scope.launch {
                val dir = getExternalFilesDir("nexus_media")
                var mediaPath: String? = null
                if (dir != null) {
                    dir.mkdirs()
                    val f = java.io.File(dir, "${System.currentTimeMillis()}_$filename")
                    f.writeBytes(data)
                    mediaPath = f.absolutePath
                }
                repository.insertMessage(MessageEntity(
                    peerAddr = dest,
                    text = "File: $filename",
                    timestamp = System.currentTimeMillis(),
                    isOutgoing = true,
                    isDirect = node.isNeighbor(destBytes) >= 0,
                    deliveryStatus = DeliveryStatus.SENT,
                    messageType = MessageType.FILE,
                    mediaPath = mediaPath,
                    fileName = filename,
                    mimeType = mimetype
                ))
            }
        }
        return ok
    }

    fun sendVoice(dest: String, data: ByteArray, durationSec: Int,
                  codec: Int = 1): Boolean {
        val destBytes = hexToBytes(dest) ?: return false
        val nxmData = NxmBuilder.buildVoice(data, durationSec, codec)
        val ok = node.sendLarge(destBytes, nxmData)
        if (ok) {
            scope.launch {
                val dir = getExternalFilesDir("nexus_media")
                var mediaPath: String? = null
                if (dir != null) {
                    dir.mkdirs()
                    val f = java.io.File(dir, "${System.currentTimeMillis()}_voice.amr")
                    f.writeBytes(data)
                    mediaPath = f.absolutePath
                }
                repository.insertMessage(MessageEntity(
                    peerAddr = dest,
                    text = "Voice note (${durationSec}s)",
                    timestamp = System.currentTimeMillis(),
                    isOutgoing = true,
                    isDirect = node.isNeighbor(destBytes) >= 0,
                    deliveryStatus = DeliveryStatus.SENT,
                    messageType = MessageType.VOICE_NOTE,
                    mediaPath = mediaPath,
                    duration = durationSec
                ))
            }
        }
        return ok
    }

    fun sendLocation(dest: String, lat: Double, lon: Double): Boolean {
        val destBytes = hexToBytes(dest) ?: return false
        val nxmData = NxmBuilder.buildLocation(lat, lon)
        val ok = node.sendSession(destBytes, nxmData) || node.send(destBytes, nxmData)
        if (ok) {
            scope.launch {
                repository.insertMessage(MessageEntity(
                    peerAddr = dest,
                    text = "Location: %.5f, %.5f".format(lat, lon),
                    timestamp = System.currentTimeMillis(),
                    isOutgoing = true,
                    isDirect = node.isNeighbor(destBytes) >= 0,
                    deliveryStatus = DeliveryStatus.SENT,
                    messageType = MessageType.LOCATION,
                    latitude = lat,
                    longitude = lon
                ))
            }
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
        scope.launch { repository.deleteConversation(peerAddr) }
    }

    fun deleteMessage(peerAddr: String, msgId: Long) {
        scope.launch { repository.deleteMessage(msgId) }
    }

    fun clearConversation(peerAddr: String) {
        scope.launch { repository.clearMessages(peerAddr) }
    }

    // --- Group operations ---

    fun getGroups(): Flow<List<com.nexus.mesh.data.GroupEntity>> =
        repository.getGroups()

    fun getGroupMessages(groupId: String): Flow<List<MessageEntity>> =
        repository.getGroupMessages(groupId)

    fun createGroup(groupIdHex: String, name: String?): Boolean {
        val groupIdBytes = hexToBytes(groupIdHex) ?: return false
        // Generate random 32-byte group key
        val key = ByteArray(32)
        java.security.SecureRandom().nextBytes(key)
        val ok = node.groupCreate(groupIdBytes, key)
        if (ok) {
            scope.launch {
                repository.ensureGroup(groupIdHex, name)
            }
        }
        return ok
    }

    fun addGroupMember(groupIdHex: String, memberAddrHex: String): Boolean {
        val groupIdBytes = hexToBytes(groupIdHex) ?: return false
        val memberBytes = hexToBytes(memberAddrHex) ?: return false
        val ok = node.groupAddMember(groupIdBytes, memberBytes)
        if (ok) {
            scope.launch {
                repository.addGroupMember(groupIdHex, memberAddrHex)
            }
        }
        return ok
    }

    fun sendGroupMessage(groupIdHex: String, text: String): Boolean {
        val groupIdBytes = hexToBytes(groupIdHex) ?: return false
        val msgId = NxmBuilder.generateMsgId()
        val nxmData = NxmBuilder.buildText(text, msgId = msgId)
        val msgIdHex = msgId.joinToString("") { "%02X".format(it) }
        val ok = node.groupSend(groupIdBytes, nxmData)
        if (ok) {
            scope.launch {
                repository.insertGroupMessage(MessageEntity(
                    peerAddr = _address.value,
                    text = text,
                    timestamp = System.currentTimeMillis(),
                    isOutgoing = true,
                    isDirect = false,
                    deliveryStatus = DeliveryStatus.SENT,
                    nxmMsgId = msgIdHex,
                    messageType = MessageType.TEXT,
                    groupId = groupIdHex
                ))
            }
        }
        return ok
    }

    fun deleteGroup(groupIdHex: String) {
        scope.launch { repository.deleteGroup(groupIdHex) }
    }

    fun setGroupName(groupIdHex: String, name: String?) {
        scope.launch { repository.setGroupName(groupIdHex, name) }
    }

    // --- Route info ---

    fun getRouteInfo(addr: String): RouteInfo? {
        val destBytes = hexToBytes(addr) ?: return null
        val info = node.getRouteInfo(destBytes) ?: return null
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

    /** Expose the underlying NexusNode so the BLE transport can inject
     *  inbound mesh packets received from the ESP32 over NUS. */
    fun getNode(): NexusNode = node

    fun getTelemetry(): NexusNode.Telemetry? = node.getTelemetry()

    fun listRoutes(): List<NexusNode.RouteRow> = node.listRoutes()

    fun listNeighborsDetailed(): List<NexusNode.NeighborRow> = node.listNeighbors()

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

    fun startUdpMulticast(userTriggered: Boolean = false): Boolean {
        if (userTriggered) udpAutoManaged = true  // user enabling means they want auto-management
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

    fun stopUdpMulticast(userTriggered: Boolean = false) {
        if (userTriggered) udpAutoManaged = false  // user disabling = don't auto-restart
        node.stopUdpMulticast()
        _udpActive.value = false
        multicastLock?.release()
        updateTransportNotification()
        Log.i(TAG, "UDP multicast stopped")
    }

    // --- Pillar management ---

    fun getPillarConfig(): Pair<Boolean, String> {
        return Pair(_pillarsEnabled.value, _pillarList.value)
    }

    fun setPillars(enabled: Boolean, pillars: String) {
        val prefs = getSharedPreferences(PREFS_PILLARS, Context.MODE_PRIVATE)
        prefs.edit()
            .putBoolean(KEY_PILLARS_ENABLED, enabled)
            .putString(KEY_PILLAR_LIST, pillars.trim())
            .apply()
        _pillarsEnabled.value = enabled
        _pillarList.value = pillars.trim()

        if (_tcpActive.value && _pillarConnected.value) {
            node.stopTcpInet()
            _tcpActive.value = false
            _pillarConnected.value = false
        }
        if (enabled && pillars.isNotBlank()) {
            connectToPillars()
        }
    }

    fun setPillarsAllowMetered(allow: Boolean) {
        val prefs = getSharedPreferences(PREFS_PILLARS, Context.MODE_PRIVATE)
        prefs.edit().putBoolean(KEY_PILLARS_ALLOW_METERED, allow).apply()
        val previous = _pillarsAllowMetered.value
        _pillarsAllowMetered.value = allow
        if (previous == allow) return

        val state = _networkState.value
        val onlyMetered = state.hasAnyInternet && !state.hasUnmetered
        if (onlyMetered) {
            if (allow && _pillarsEnabled.value && !_pillarConnected.value &&
                    _pillarList.value.isNotBlank()) {
                Log.i(TAG, "Pillars: metered now allowed, attempting connect")
                connectToPillars()
            } else if (!allow && _pillarConnected.value) {
                Log.i(TAG, "Pillars: metered now disallowed, dropping connection")
                node.stopTcpInet()
                _tcpActive.value = false
                _pillarConnected.value = false
            }
        }
    }

    fun addPillar(hostPort: String) {
        val current = _pillarList.value
        val newList = if (current.isBlank()) hostPort.trim()
                      else "$current, ${hostPort.trim()}"
        setPillars(true, newList)
    }

    fun removePillar(hostPort: String) {
        val current = _pillarList.value
        val newList = current.split(",")
            .map { it.trim() }
            .filter { it != hostPort.trim() }
            .joinToString(", ")
        setPillars(_pillarsEnabled.value, newList)
    }

    // --- Nicknames (now backed by Room conversations table) ---

    fun getDisplayName(addr: String): String {
        // Synchronous check -- read from in-memory cache or just return addr
        // For proper display, the UI should use the conversation's nickname from Flow
        return addr
    }

    fun setNickname(addr: String, name: String) {
        scope.launch {
            repository.setNickname(addr, if (name.isBlank()) null else name.trim())
        }
    }

    fun setMyName(name: String) {
        val trimmed = name.trim()
        val changed = _myName.value != trimmed
        _myName.value = trimmed
        val prefs = getSharedPreferences(PREFS_NICKNAMES, Context.MODE_PRIVATE)
        prefs.edit().putString(KEY_MY_NAME, trimmed).apply()
        if (changed) {
            // Force re-announce: every currently-known neighbor needs the
            // new name. Clearing the set lets onNeighbor (or the loop below)
            // send a fresh NICKNAME on the next tick.
            nicknameSentTo.clear()
            if (trimmed.isNotBlank()) broadcastNicknameToNeighbors()
        }
    }

    /** Push the current nickname to every currently-known neighbor right
     *  now (in addition to the on-announce path). Called when the user
     *  changes their name in Settings so peers update without waiting for
     *  the next announce cycle. */
    private fun broadcastNicknameToNeighbors() {
        val snapshot = _neighbors.value
        for (n in snapshot) {
            if (n.role >= NexusNode.ROLE_PILLAR) continue
            val addr = hexToBytes(n.addr) ?: continue
            sendNicknameIfNeeded(addr, n.addr)
        }
    }

    private fun loadMyName() {
        val prefs = getSharedPreferences(PREFS_NICKNAMES, Context.MODE_PRIVATE)
        _myName.value = prefs.getString(KEY_MY_NAME, "") ?: ""
    }

    /** Persist node role. Applies on next service start — the underlying
     *  C node is created with the role at init, so changing it at runtime
     *  would require tearing the node down and rebuilding it. */
    fun setRole(role: Int) {
        _role.value = role
        getSharedPreferences(PREFS_ROLE, Context.MODE_PRIVATE)
            .edit().putInt(KEY_ROLE, role).apply()
    }

    private fun loadRole() {
        val prefs = getSharedPreferences(PREFS_ROLE, Context.MODE_PRIVATE)
        _role.value = prefs.getInt(KEY_ROLE, DEFAULT_ROLE)
    }

    // --- Helpers ---

    private fun updateTransportNotification() {
        val parts = mutableListOf("Node: ${_address.value}")
        if (_tcpActive.value) parts.add("TCP")
        if (_udpActive.value) parts.add("UDP")
        val net = _networkState.value
        if (net.hasAnyInternet) {
            val netTypes = net.interfaces.map { it.type }.distinct()
            parts.add(netTypes.joinToString("+"))
        }
        updateNotification(parts.joinToString(" | "))
    }

    private fun hexToBytes(hex: String): ByteArray? {
        if (hex.length != 8) return null
        return try {
            ByteArray(4) { hex.substring(it * 2, it * 2 + 2).toInt(16).toByte() }
        } catch (e: Exception) { Log.w(TAG, "decode failed", e); null }
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

    /**
     * PendingIntent that brings MainActivity to the foreground when
     * the user taps the notification.
     *
     * SINGLE_TOP + CLEAR_TOP avoids stacking duplicate Activity
     * instances on top of an already-running app. FLAG_IMMUTABLE is
     * required from API 31+.
     */
    private fun openAppPendingIntent(): PendingIntent {
        val intent = Intent(this, MainActivity::class.java).apply {
            flags = Intent.FLAG_ACTIVITY_SINGLE_TOP or Intent.FLAG_ACTIVITY_CLEAR_TOP
        }
        val flags = PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE
        return PendingIntent.getActivity(this, 0, intent, flags)
    }

    private fun buildNotification(text: String): Notification {
        return NotificationCompat.Builder(this, CHANNEL_ID)
            .setContentTitle("NEXUS Mesh")
            .setContentText(text)
            .setSmallIcon(R.drawable.ic_mesh)
            .setOngoing(true)
            .setContentIntent(openAppPendingIntent())
            .build()
    }

    private fun updateNotification(text: String) {
        val mgr = getSystemService(NotificationManager::class.java)
        mgr.notify(NOTIFICATION_ID, buildNotification(text))
    }

    private fun showMessageNotification(from: String, text: String) {
        val notification = NotificationCompat.Builder(this, CHANNEL_ID)
            .setContentTitle("Message from $from")
            .setContentText(text.take(100))
            .setSmallIcon(R.drawable.ic_mesh)
            .setAutoCancel(true)
            .setContentIntent(openAppPendingIntent())
            .build()
        val mgr = getSystemService(NotificationManager::class.java)
        mgr.notify(from.hashCode(), notification)
    }
}
