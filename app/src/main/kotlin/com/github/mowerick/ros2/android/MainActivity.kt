package com.github.mowerick.ros2.android

import android.Manifest
import android.content.pm.PackageManager
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.BackHandler
import androidx.activity.compose.setContent
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.ui.Modifier
import androidx.core.content.ContextCompat
import androidx.lifecycle.viewmodel.compose.viewModel
import com.github.mowerick.ros2.android.ui.components.NotificationOverlay
import com.github.mowerick.ros2.android.ui.screens.BuiltInSensorsScreen
import com.github.mowerick.ros2.android.ui.screens.CameraDetailScreen
import com.github.mowerick.ros2.android.ui.screens.DashboardScreen
import com.github.mowerick.ros2.android.ui.screens.NodeDetailScreen
import com.github.mowerick.ros2.android.ui.screens.RosSetupScreen
import com.github.mowerick.ros2.android.ui.screens.SensorDetailScreen
import com.github.mowerick.ros2.android.ui.screens.SubsystemScreen
import com.github.mowerick.ros2.android.interfaces.NetworkInterfaceProvider
import com.github.mowerick.ros2.android.interfaces.PermissionHandler
import com.github.mowerick.ros2.android.ui.theme.Ros2AndroidTheme
import com.github.mowerick.ros2.android.viewmodel.RosViewModel
import com.github.mowerick.ros2.android.viewmodel.RosViewModelFactory
import com.github.mowerick.ros2.android.viewmodel.Screen
import java.net.NetworkInterface

class MainActivity : ComponentActivity(), PermissionHandler, NetworkInterfaceProvider {

    private var currentViewModel: RosViewModel? = null

    private val requestCameraPermission =
        registerForActivityResult(ActivityResultContracts.RequestPermission()) { granted ->
            NativeBridge.nativeOnPermissionResult("CAMERA", granted)
            // After camera permission, check location permission
            checkLocationPermission()
        }

    private val requestLocationPermissionLauncher =
        registerForActivityResult(ActivityResultContracts.RequestPermission()) { granted ->
            NativeBridge.nativeOnPermissionResult("LOCATION", granted)
            if (granted) {
                currentViewModel?.onLocationPermissionGranted()
            } else {
                currentViewModel?.onLocationPermissionDenied()
            }
        }

    private val locationSettingsLauncher =
        registerForActivityResult(ActivityResultContracts.StartIntentSenderForResult()) { result ->
            android.util.Log.i("MainActivity", "Location settings result: ${result.resultCode}")
            if (result.resultCode == RESULT_OK) {
                android.util.Log.i("MainActivity", "Location settings enabled by user")
                currentViewModel?.onLocationSettingsEnabled()
            } else {
                android.util.Log.w("MainActivity", "User declined to enable location settings")
                currentViewModel?.onLocationSettingsCancelled()
            }
        }

    // PermissionHandler implementation
    override fun requestLocationPermission() {
        requestLocationPermissionLauncher.launch(Manifest.permission.ACCESS_FINE_LOCATION)
    }

    override fun hasLocationPermission(): Boolean {
        return ContextCompat.checkSelfPermission(this, Manifest.permission.ACCESS_FINE_LOCATION) ==
            PackageManager.PERMISSION_GRANTED
    }

    override fun getLocationSettingsLauncher() = locationSettingsLauncher

    // NetworkInterfaceProvider implementation
    override fun queryNetworkInterfaces(): Array<String> {
        return try {
            NetworkInterface.getNetworkInterfaces()
                ?.toList()
                ?.filter { iface ->
                    iface.isUp &&
                    !iface.isLoopback &&
                    !iface.isPointToPoint &&
                    iface.supportsMulticast() &&
                    // Exclude virtual/cellular interfaces
                    !iface.name.startsWith("rmnet") &&
                    !iface.name.startsWith("dummy") &&
                    !iface.name.startsWith("tun") &&
                    !iface.name.startsWith("ppp")
                }
                ?.map { it.name }
                ?.toTypedArray()
                ?: emptyArray()
        } catch (_: Exception) {
            emptyArray()
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        NativeBridge.nativeInit(cacheDir.absolutePath, packageName)
        NativeBridge.nativeSetNetworkInterfaces(queryNetworkInterfaces())

        // Request camera permission first, then location in the callback
        if (ContextCompat.checkSelfPermission(this, Manifest.permission.CAMERA)
            != PackageManager.PERMISSION_GRANTED
        ) {
            requestCameraPermission.launch(Manifest.permission.CAMERA)
        } else {
            NativeBridge.nativeOnPermissionResult("CAMERA", true)
            checkLocationPermission()
        }

        setContent {
            Ros2AndroidTheme {
                val vm: RosViewModel = viewModel(
                    factory = RosViewModelFactory(
                        applicationContext,
                        this@MainActivity,  // PermissionHandler
                        this@MainActivity   // NetworkInterfaceProvider
                    )
                )
                LaunchedEffect(Unit) {
                    currentViewModel = vm
                    vm.loadNetworkInterfaces()
                }

                val screen by vm.screen.collectAsState()
                val rosStarted by vm.rosStarted.collectAsState()
                val rosDomainId by vm.rosDomainId.collectAsState()
                val deviceId by vm.deviceId.collectAsState()
                val sensors by vm.sensors.collectAsState()
                val cameras by vm.cameras.collectAsState()
                val reading by vm.currentReading.collectAsState()
                val networkInterfaces by vm.networkInterfaces.collectAsState()
                val selectedNetworkInterface by vm.selectedNetworkInterface.collectAsState()
                val pipelineNodes by vm.pipelineNodes.collectAsState()
                val isProbing by vm.isProbing.collectAsState()
                val cameraFrame by vm.cameraFrame.collectAsState()
                val activeNotifications by vm.notifications.collectAsState()

                // Handle back button: navigate back if in submenu, exit if on Dashboard
                BackHandler(enabled = screen != Screen.Dashboard) {
                    vm.navigateBack()
                }

                Box(modifier = Modifier.fillMaxSize()) {
                when (val s = screen) {
                    is Screen.Dashboard -> DashboardScreen(
                        rosStarted = rosStarted,
                        rosDomainId = rosDomainId,
                        sensorCount = sensors.size,
                        cameraCount = cameras.size,
                        onSettingsClick = { vm.navigateToRosSetup() },
                        onBuiltInSensorsClick = { vm.navigateToBuiltInSensors() },
                        onSubsystemClick = { vm.navigateToSubsystem() }
                    )
                    is Screen.RosSetup -> RosSetupScreen(
                        rosStarted = rosStarted,
                        networkInterfaces = networkInterfaces,
                        rosDomainId = rosDomainId,
                        deviceId = deviceId,
                        selectedNetworkInterface = selectedNetworkInterface,
                        onBack = { vm.navigateBack() },
                        onStartRos = { domainId, iface, devId -> vm.startRos(domainId, iface, devId) },
                        onRestartRos = { domainId, iface, devId -> vm.restartRos(domainId, iface, devId) },
                        onStopRos = { vm.stopRos() },
                        onRefreshInterfaces = { vm.refreshNetworkInterfaces() },
                        onDomainIdChanged = { vm.setDomainId(it) },
                        onDeviceIdChanged = { vm.setDeviceId(it) }
                    )
                    is Screen.BuiltInSensors -> BuiltInSensorsScreen(
                        sensors = sensors,
                        cameras = cameras,
                        onBack = { vm.navigateBack() },
                        onSensorClick = { vm.navigateToSensor(it) },
                        onCameraClick = { vm.navigateToCamera(it) },
                        onSensorToggle = { sensor, enable ->
                            if (enable) vm.enableSensor(sensor.uniqueId)
                            else vm.disableSensor(sensor.uniqueId)
                        },
                        onCameraToggle = { camera, enable ->
                            if (enable) vm.enableCamera(camera.uniqueId)
                            else vm.disableCamera(camera.uniqueId)
                        }
                    )
                    is Screen.SensorDetail -> {
                        val sensor = sensors.find { it.uniqueId == s.sensorId }
                        if (sensor != null) {
                            SensorDetailScreen(
                                sensor = sensor,
                                reading = reading,
                                onBack = { vm.navigateBack() },
                                onEnable = { vm.enableSensor(sensor.uniqueId) },
                                onDisable = { vm.disableSensor(sensor.uniqueId) }
                            )
                        }
                    }
                    is Screen.CameraDetail -> {
                        val camera = cameras.find { it.uniqueId == s.cameraId }
                        if (camera != null) {
                            CameraDetailScreen(
                                camera = camera,
                                cameraFrame = cameraFrame,
                                onBack = { vm.navigateBack() },
                                onEnable = { vm.enableCamera(camera.uniqueId) },
                                onDisable = { vm.disableCamera(camera.uniqueId) }
                            )
                        }
                    }
                    is Screen.Subsystem -> SubsystemScreen(
                        nodes = pipelineNodes,
                        onBack = { vm.navigateBack() },
                        onNodeClick = { vm.navigateToNode(it) },
                        onNodeStartStop = { vm.toggleNodeState(it) },
                        isNodeStartable = { vm.isNodeStartable(it) },
                        isProbing = isProbing,
                        onToggleProbing = { vm.toggleTopicProbing() }
                    )
                    is Screen.NodeDetail -> {
                        val node = pipelineNodes.find { it.id == s.nodeId }
                        if (node != null) {
                            NodeDetailScreen(
                                node = node,
                                canStart = vm.isNodeStartable(node.id),
                                onBack = { vm.navigateBack() },
                                onStartStop = { vm.toggleNodeState(node.id) }
                            )
                        }
                    }
                }
                NotificationOverlay(
                    notifications = activeNotifications,
                    onDismiss = { vm.dismissNotification(it) }
                )
                }
            }
        }
    }

    override fun onDestroy() {
        NativeBridge.nativeDestroy()
        currentViewModel = null
        super.onDestroy()
    }

    private fun checkLocationPermission() {
        if (ContextCompat.checkSelfPermission(this, Manifest.permission.ACCESS_FINE_LOCATION)
            != PackageManager.PERMISSION_GRANTED
        ) {
            requestLocationPermissionLauncher.launch(Manifest.permission.ACCESS_FINE_LOCATION)
        } else {
            NativeBridge.nativeOnPermissionResult("LOCATION", true)
        }
    }
}
