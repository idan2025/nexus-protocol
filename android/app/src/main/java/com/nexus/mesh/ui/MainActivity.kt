package com.nexus.mesh.ui

import android.Manifest
import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.content.ServiceConnection
import android.content.pm.PackageManager
import android.os.Bundle
import android.os.IBinder
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
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
import com.nexus.mesh.service.NexusService

class MainActivity : ComponentActivity() {

    private var nexusService: NexusService? = null
    private var bound = false

    private val connection = object : ServiceConnection {
        override fun onServiceConnected(name: ComponentName?, binder: IBinder?) {
            nexusService = (binder as NexusService.LocalBinder).getService()
            bound = true
        }
        override fun onServiceDisconnected(name: ComponentName?) {
            nexusService = null
            bound = false
        }
    }

    private val permissionLauncher = registerForActivityResult(
        ActivityResultContracts.RequestMultiplePermissions()
    ) { /* permissions granted or denied */ }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        requestPermissions()

        val intent = Intent(this, NexusService::class.java)
        startForegroundService(intent)
        bindService(intent, connection, Context.BIND_AUTO_CREATE)

        setContent {
            NexusApp(this)
        }
    }

    override fun onDestroy() {
        if (bound) unbindService(connection)
        super.onDestroy()
    }

    fun getService(): NexusService? = nexusService

    private fun requestPermissions() {
        val needed = mutableListOf<String>()
        val perms = arrayOf(
            Manifest.permission.BLUETOOTH_SCAN,
            Manifest.permission.BLUETOOTH_CONNECT,
            Manifest.permission.BLUETOOTH_ADVERTISE,
            Manifest.permission.ACCESS_FINE_LOCATION,
            Manifest.permission.POST_NOTIFICATIONS,
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

    // Hide bottom bar on conversation detail screen
    val showBottomBar = !currentRoute.startsWith("conversation/")

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
            NavHost(
                navController,
                startDestination = "chat",
                modifier = Modifier.padding(padding)
            ) {
                composable("chat") { ChatScreen(activity, navController) }
                composable("mesh") { MeshScreen(activity, navController) }
                composable("devices") { DevicesScreen(activity) }
                composable("settings") { SettingsScreen(activity) }
                composable(
                    "conversation/{address}",
                    arguments = listOf(navArgument("address") { type = NavType.StringType })
                ) { backStackEntry ->
                    val address = backStackEntry.arguments?.getString("address") ?: ""
                    ConversationScreen(activity, navController, address)
                }
            }
        }
    }
}
