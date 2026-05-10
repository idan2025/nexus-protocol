package com.nexus.mesh.ui

import android.Manifest
import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.content.ServiceConnection
import android.content.pm.PackageManager
import android.net.Uri
import android.os.Bundle
import android.os.IBinder
import android.os.PowerManager
import android.provider.Settings
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.result.ActivityResultLauncher
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.*
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.core.content.ContextCompat
import androidx.navigation.NavType
import androidx.navigation.compose.NavHost
import androidx.navigation.compose.composable
import androidx.navigation.compose.currentBackStackEntryAsState
import androidx.navigation.compose.rememberNavController
import androidx.navigation.navArgument
import com.nexus.mesh.ble.BleTransport
import com.nexus.mesh.service.NexusService
import com.nexus.mesh.updater.UpdateAvailableDialog
import com.nexus.mesh.updater.UpdateBanner
import com.nexus.mesh.updater.UpdateInstaller
import com.nexus.mesh.updater.UpdateScheduler
import com.nexus.mesh.updater.rememberUpdateState
import com.journeyapps.barcodescanner.ScanContract
import com.journeyapps.barcodescanner.ScanOptions
import kotlinx.coroutines.launch

private const val PREFS_STARTUP = "nexus_startup"
private const val KEY_BATTERY_DISMISSED = "battery_opt_dismissed"

class MainActivity : ComponentActivity() {

    private var nexusService: NexusService? = null
    private var bound = false

    // QR scan result handler
    var qrScanLauncher: ActivityResultLauncher<ScanOptions>? = null
        private set
    private var qrScanCallback: ((String?) -> Unit)? = null

    private val connection = object : ServiceConnection {
        override fun onServiceConnected(name: ComponentName?, binder: IBinder?) {
            nexusService = (binder as NexusService.LocalBinder).getService()
            bound = true
            // Wire the activity-scoped BLE transport to the freshly-bound
            // node so the outbound drain pump can deliver packets even
            // when the user is on a tab other than Devices.
            bleTransport.nexusNode = nexusService?.getNode()
            // Surface the BLE-bridge connection state to the service so
            // the Network Interfaces card shows a "LoRa (BLE)" row while
            // the phone is paired with an ESP32 / nRF radio node.
            startBleBridgeWatcher()
        }
        override fun onServiceDisconnected(name: ComponentName?) {
            bleBridgeWatcher?.cancel()
            bleBridgeWatcher = null
            nexusService = null
            bound = false
        }
    }

    private val activityScope = kotlinx.coroutines.CoroutineScope(
        kotlinx.coroutines.SupervisorJob() + kotlinx.coroutines.Dispatchers.Main.immediate
    )
    private var bleBridgeWatcher: kotlinx.coroutines.Job? = null
    private fun startBleBridgeWatcher() {
        bleBridgeWatcher?.cancel()
        bleBridgeWatcher = activityScope.launch {
            bleTransport.connectedDevice.collect { name ->
                nexusService?.setBleBridge(name)
            }
        }
    }

    /**
     * Activity-scoped BLE transport. Survives navigation between bottom-bar
     * tabs and minimisation -- the GATT connection and the outbound drain
     * pump live as long as the activity does, so the connection chip stays
     * stable when the user moves around the app.
     *
     * Lazily created on first read (Devices tab opens first, or
     * onServiceConnected fires first -- whichever).
     */
    val bleTransport: BleTransport by lazy { BleTransport(this) }

    private val permissionLauncher = registerForActivityResult(
        ActivityResultContracts.RequestMultiplePermissions()
    ) { /* permissions granted or denied */ }

    var showBatteryPrompt by mutableStateOf(false)
        private set

    /** Flipped to true when the user taps the scheduled-update notification.
     *  Forces [UpdateAvailableDialog] open even if the user previously
     *  snoozed or scheduled this tag. */
    var forceShowUpdateDialog by mutableStateOf(false)

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        // Register QR scanner launcher
        qrScanLauncher = registerForActivityResult(ScanContract()) { result ->
            val contents = result.contents ?: return@registerForActivityResult
            when {
                contents.startsWith("nexus://") -> {
                    // Parse nexus://ADDRESS/PUBKEY format (identity exchange)
                    val parts = contents.removePrefix("nexus://").split("/")
                    if (parts.isNotEmpty() && parts[0].length == 8) {
                        qrScanCallback?.invoke(parts[0])
                    }
                }
                contents.startsWith(com.nexus.mesh.data.PaperMessage.URI_PREFIX) -> {
                    importPaperMessage(contents)
                }
            }
        }

        requestPermissions()

        // Sweep stale on-disk caches:
        //   - cache/apk/  : leftover update APKs the system installer
        //                   already consumed last run
        //   - files/firmware/<old-tags>/ : firmware images from
        //                   superseded releases
        // (Best-effort; a failure here must never block startup.)
        try {
            UpdateInstaller(this).cleanCache()
            // Don't aggressively wipe firmware here -- the user may
            // have just downloaded an image to flash and not yet
            // launched the flasher; FirmwareDownloader.fetch() prunes
            // stale tags itself when it next runs.
        } catch (e: Exception) {
            android.util.Log.w("NexusMesh", "Cache cleanup failed", e)
        }

        val intent = Intent(this, NexusService::class.java)
        startForegroundService(intent)
        bindService(intent, connection, Context.BIND_AUTO_CREATE)

        showBatteryPrompt = shouldPromptBatteryOptimization()
        forceShowUpdateDialog = intent?.getBooleanExtra(UpdateScheduler.EXTRA_SHOW_UPDATE, false) == true

        setContent {
            NexusApp(this)
        }
    }

    override fun onNewIntent(intent: Intent) {
        super.onNewIntent(intent)
        setIntent(intent)
        if (intent.getBooleanExtra(UpdateScheduler.EXTRA_SHOW_UPDATE, false)) {
            forceShowUpdateDialog = true
        }
    }

    private fun shouldPromptBatteryOptimization(): Boolean {
        val prefs = getSharedPreferences(PREFS_STARTUP, Context.MODE_PRIVATE)
        if (prefs.getBoolean(KEY_BATTERY_DISMISSED, false)) return false
        val pm = getSystemService(Context.POWER_SERVICE) as? PowerManager ?: return false
        return !pm.isIgnoringBatteryOptimizations(packageName)
    }

    fun requestIgnoreBatteryOptimizations() {
        val intent = Intent(Settings.ACTION_REQUEST_IGNORE_BATTERY_OPTIMIZATIONS).apply {
            data = Uri.parse("package:$packageName")
        }
        try {
            startActivity(intent)
        } catch (e: Exception) {
            // Fall back to the generic battery-optimization settings list if the
            // direct-request intent isn't available on this OEM.
            startActivity(Intent(Settings.ACTION_IGNORE_BATTERY_OPTIMIZATION_SETTINGS))
        }
        dismissBatteryPrompt(persist = true)
    }

    fun dismissBatteryPrompt(persist: Boolean) {
        showBatteryPrompt = false
        if (persist) {
            getSharedPreferences(PREFS_STARTUP, Context.MODE_PRIVATE)
                .edit().putBoolean(KEY_BATTERY_DISMISSED, true).apply()
        }
    }

    override fun onDestroy() {
        bleBridgeWatcher?.cancel()
        activityScope.cancel()
        if (bound) unbindService(connection)
        super.onDestroy()
    }

    fun getService(): NexusService? = nexusService

    /**
     * The BLE address the app is currently connected to (used by the
     * Flash Node screen to pick a target for BLE DFU). Backed directly
     * by the activity-scoped [bleTransport] so it stays correct across
     * tabs without needing a separate cache.
     */
    fun bleConnectedAddress(): String? = bleTransport.connectedDevice.value
    fun setBleConnectedAddress(addr: String?) { /* no-op, retained for compatibility */ }

    fun setQrScanCallback(callback: (String?) -> Unit) {
        qrScanCallback = callback
    }

    private fun importPaperMessage(uri: String) {
        val env = com.nexus.mesh.data.PaperMessage.decode(uri) ?: return
        val svc = nexusService ?: return
        val repo = svc.repository
        kotlinx.coroutines.CoroutineScope(kotlinx.coroutines.Dispatchers.IO).launch {
            repo.ensureConversation(env.fromAddr)
            repo.insertMessage(
                com.nexus.mesh.data.MessageEntity(
                    peerAddr = env.fromAddr,
                    text = env.text,
                    timestamp = env.timestampMs,
                    isOutgoing = false,
                    isDirect = false,
                    deliveryStatus = com.nexus.mesh.data.DeliveryStatus.DELIVERED,
                    messageType = com.nexus.mesh.data.MessageType.TEXT
                )
            )
        }
    }

    private fun requestPermissions() {
        val needed = mutableListOf<String>()
        val perms = arrayOf(
            Manifest.permission.BLUETOOTH_SCAN,
            Manifest.permission.BLUETOOTH_CONNECT,
            Manifest.permission.BLUETOOTH_ADVERTISE,
            Manifest.permission.ACCESS_FINE_LOCATION,
            Manifest.permission.POST_NOTIFICATIONS,
            Manifest.permission.CAMERA,
            Manifest.permission.RECORD_AUDIO,
        )
        for (p in perms) {
            if (ContextCompat.checkSelfPermission(this, p) != PackageManager.PERMISSION_GRANTED) {
                needed.add(p)
            }
        }
        if (needed.isNotEmpty()) {
            permissionLauncher.launch(needed.toTypedArray())
        }
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun NexusApp(activity: MainActivity) {
    val navController = rememberNavController()
    val navBackStackEntry by navController.currentBackStackEntryAsState()
    val currentRoute = navBackStackEntry?.destination?.route ?: "chat"

    // Updater state — shared across the home banner and SettingsScreen.
    val updateState = rememberUpdateState(activity)
    LaunchedEffect(Unit) { updateState.refresh(force = false) }

    // Hide bottom bar on detail screens
    val showBottomBar = currentRoute in listOf("chat", "mesh", "devices", "settings")

    val selectedTab = when {
        currentRoute == "chat" -> 0
        currentRoute == "mesh" -> 1
        currentRoute == "devices" -> 2
        currentRoute == "settings" -> 3
        else -> 0
    }

    MaterialTheme(
        colorScheme = darkColorScheme()
    ) {
        Scaffold(
            bottomBar = {
                if (showBottomBar) {
                    NavigationBar {
                        NavigationBarItem(
                            icon = { Icon(Icons.Default.Email, "Chat") },
                            label = { Text("Chat") },
                            selected = selectedTab == 0,
                            onClick = {
                                navController.navigate("chat") {
                                    popUpTo("chat") { inclusive = true }
                                    launchSingleTop = true
                                }
                            }
                        )
                        NavigationBarItem(
                            icon = { Icon(Icons.Default.Share, "Mesh") },
                            label = { Text("Mesh") },
                            selected = selectedTab == 1,
                            onClick = {
                                navController.navigate("mesh") {
                                    popUpTo("chat")
                                    launchSingleTop = true
                                }
                            }
                        )
                        NavigationBarItem(
                            icon = { Icon(Icons.Default.Phone, "Devices") },
                            label = { Text("Devices") },
                            selected = selectedTab == 2,
                            onClick = {
                                navController.navigate("devices") {
                                    popUpTo("chat")
                                    launchSingleTop = true
                                }
                            }
                        )
                        NavigationBarItem(
                            icon = { Icon(Icons.Default.Settings, "Settings") },
                            label = { Text("Settings") },
                            selected = selectedTab == 3,
                            onClick = {
                                navController.navigate("settings") {
                                    popUpTo("chat")
                                    launchSingleTop = true
                                }
                            }
                        )
                    }
                }
            }
        ) { padding ->
            Column(modifier = Modifier.padding(padding).fillMaxSize()) {
                // Update banner is rendered above all routes so it stays
                // visible while the user navigates.
                UpdateBanner(state = updateState, activity = activity)
                NavHost(
                    navController,
                    startDestination = "chat",
                    modifier = Modifier.weight(1f)
                ) {
                composable("chat") { ChatScreen(activity, navController) }
                composable("mesh") { MeshScreen(activity, navController) }
                composable("devices") { DevicesScreen(activity) }
                composable("settings") { SettingsScreen(activity, navController, updateState) }
                composable("flash_node") { FlashNodeScreen(activity) }
                composable(
                    "conversation/{address}",
                    arguments = listOf(navArgument("address") { type = NavType.StringType })
                ) { backStackEntry ->
                    val address = backStackEntry.arguments?.getString("address") ?: ""
                    ConversationScreen(activity, navController, address)
                }
                composable("qr") { QrScreen(activity, navController) }
                composable("announce_stream") { AnnounceStreamScreen(activity, navController) }
                composable("contacts") { ContactsScreen(activity, navController) }
                composable("search") { SearchScreen(activity, navController) }
                composable("paper") { PaperMessageScreen(activity, navController) }
                composable("telemetry") { TelemetryScreen(activity, navController) }
                composable("routes") { RouteInspectorScreen(activity, navController) }
                composable(
                    "map/{lat}/{lon}",
                    arguments = listOf(
                        navArgument("lat") { type = NavType.StringType },
                        navArgument("lon") { type = NavType.StringType }
                    )
                ) { backStackEntry ->
                    val lat = backStackEntry.arguments?.getString("lat")?.toDoubleOrNull() ?: 0.0
                    val lon = backStackEntry.arguments?.getString("lon")?.toDoubleOrNull() ?: 0.0
                    MapScreen(navController, lat, lon)
                }
                composable(
                    "group_conversation/{groupId}",
                    arguments = listOf(navArgument("groupId") { type = NavType.StringType })
                ) { backStackEntry ->
                    val groupId = backStackEntry.arguments?.getString("groupId") ?: ""
                    GroupConversationScreen(activity, navController, groupId)
                }
                composable(
                    "group_info/{groupId}",
                    arguments = listOf(navArgument("groupId") { type = NavType.StringType })
                ) { backStackEntry ->
                    val groupId = backStackEntry.arguments?.getString("groupId") ?: ""
                    GroupInfoScreen(activity, navController, groupId)
                }
                }   // NavHost
            }       // Column
        }

        if (activity.showBatteryPrompt) {
            AlertDialog(
                onDismissRequest = { activity.dismissBatteryPrompt(persist = false) },
                title = { Text("Keep NEXUS running?") },
                text = {
                    Text(
                        "Android's battery optimizer silently kills background mesh " +
                            "services, which stops delivery of messages while your " +
                            "screen is off. Allow NEXUS to run in the background to " +
                            "stay reachable."
                    )
                },
                confirmButton = {
                    TextButton(onClick = { activity.requestIgnoreBatteryOptimizations() }) {
                        Text("Allow")
                    }
                },
                dismissButton = {
                    TextButton(onClick = { activity.dismissBatteryPrompt(persist = true) }) {
                        Text("Not now")
                    }
                }
            )
        }

        // Update-available popup with changelog + Update Now / Later / Schedule.
        // Auto-shown once per app session when a newer release is fetched and
        // the user hasn't already snoozed / scheduled / skipped this tag.
        UpdateAvailableDialog(
            state = updateState,
            activity = activity,
            forceShow = activity.forceShowUpdateDialog
        )
    }
}
