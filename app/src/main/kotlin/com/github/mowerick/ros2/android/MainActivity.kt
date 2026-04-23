package com.github.mowerick.ros2.android

import android.Manifest
import android.content.Intent
import android.content.pm.ActivityInfo
import androidx.core.view.WindowCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.WindowInsetsControllerCompat
import android.content.pm.PackageManager
import android.hardware.usb.UsbDevice
import android.hardware.usb.UsbManager
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.BackHandler
import androidx.activity.compose.setContent
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.material3.Text
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.ui.Modifier
import androidx.core.content.ContextCompat
import androidx.lifecycle.viewmodel.compose.viewModel
import com.github.mowerick.ros2.android.ui.components.NotificationOverlay
import com.github.mowerick.ros2.android.ui.screens.BeetlePredatorScreen
import com.github.mowerick.ros2.android.ui.screens.BuiltInSensorsScreen
import com.github.mowerick.ros2.android.ui.screens.CameraDetailScreen
import com.github.mowerick.ros2.android.ui.screens.DashboardScreen
import com.github.mowerick.ros2.android.ui.screens.ExternalSensorsScreen
import com.github.mowerick.ros2.android.ui.screens.FullscreenDebugVisualizationScreen
import com.github.mowerick.ros2.android.ui.screens.LidarDetailScreen
import com.github.mowerick.ros2.android.ui.screens.NodeDetailScreen
import com.github.mowerick.ros2.android.ui.screens.RosSetupScreen
import com.github.mowerick.ros2.android.ui.screens.SensorDetailScreen
import com.github.mowerick.ros2.android.ui.screens.SubsystemScreen
import com.github.mowerick.ros2.android.interfaces.NetworkInterfaceProvider
import com.github.mowerick.ros2.android.interfaces.PermissionHandler
import com.github.mowerick.ros2.android.ui.theme.Ros2AndroidTheme
import com.github.mowerick.ros2.android.util.NativeBridge
import com.github.mowerick.ros2.android.util.UsbSerialBridge
import com.github.mowerick.ros2.android.viewmodel.RosViewModel
import com.github.mowerick.ros2.android.viewmodel.RosViewModelFactory
import com.github.mowerick.ros2.android.viewmodel.Screen
import java.io.File
import java.net.NetworkInterface
import java.security.MessageDigest

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

    override fun onNewIntent(intent: Intent?) {
        super.onNewIntent(intent)
        setIntent(intent) // Update activity's intent

        // Handle USB device attachment while app is running
        if (intent?.action == UsbManager.ACTION_USB_DEVICE_ATTACHED) {
            val device: UsbDevice? = intent.getParcelableExtra(UsbManager.EXTRA_DEVICE, UsbDevice::class.java)
            android.util.Log.i("MainActivity", "USB device attached while app running: ${device?.deviceName}")
            device?.let {
                currentViewModel?.handleUsbDeviceAttached(it)
            }
        }
    }

    /**
     * Copy NCNN model files from assets to internal storage
     * Uses hash-based validation to update only changed models
     */
    private fun copyModelsToInternalStorage() {
        val modelsDir = filesDir.resolve("models")
        modelsDir.mkdirs()

        val modelFiles = listOf(
            "yolov9_s_pobed.ncnn.param",
            "yolov9_s_pobed.ncnn.bin",
            "osnet_ain_x1_0.ncnn.param",
            "osnet_ain_x1_0.ncnn.bin"
        )

        android.util.Log.i("MainActivity", "Validating models in ${modelsDir.absolutePath}")

        modelFiles.forEach { filename ->
            try {
                val assetHash = computeAssetHash("models/$filename")
                val fileHash = computeFileHash(modelsDir.resolve(filename))


                if (assetHash != fileHash) {
                    android.util.Log.i("MainActivity", "Updating model: $filename (hash mismatch)")
                    assets.open("models/$filename").use { input ->
                        modelsDir.resolve(filename).outputStream().use { output ->
                            input.copyTo(output)
                        }
                    }
                } else {
                    android.util.Log.d("MainActivity", "Model up-to-date: $filename")
                }
            } catch (e: Exception) {
                android.util.Log.e("MainActivity", "Failed to update $filename", e)
            }
        }

        android.util.Log.i("MainActivity", "Model validation complete")
    }

    /**
     * Compute SHA-256 hash of an input stream
     */
    private fun computeHash(inputStream: java.io.InputStream): String {
        val digest = MessageDigest.getInstance("SHA-256")
        val buffer = ByteArray(8192)
        var read: Int
        while (inputStream.read(buffer).also { read = it } != -1) {
            digest.update(buffer, 0, read)
        }
        return digest.digest().joinToString("") { "%02x".format(it) }
    }

    private fun computeAssetHash(assetPath: String): String =
        assets.open(assetPath).use { computeHash(it) }

    private fun computeFileHash(file: File): String =
        if (!file.exists()) "" else file.inputStream().use { computeHash(it) }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        NativeBridge.nativeInit(cacheDir.absolutePath, packageName)

        // Copy NCNN models from assets to internal storage (one-time operation)
        copyModelsToInternalStorage()

        // Initialize USB Serial JNI bridge (must be called after nativeInit)
        UsbSerialBridge.nativeInitJNI()

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
                        this@MainActivity,  // NetworkInterfaceProvider
                        initialScreen = if (intent.getBooleanExtra("navigate_to_settings", false)) {
                            Screen.RosSetup
                        } else {
                            Screen.Dashboard
                        }
                    )
                )
                LaunchedEffect(Unit) {
                    currentViewModel = vm
                    vm.loadNetworkInterfaces()

                    // Handle USB device if app was launched by USB intent
                    if (intent?.action == UsbManager.ACTION_USB_DEVICE_ATTACHED) {
                        val device: UsbDevice? = intent.getParcelableExtra(UsbManager.EXTRA_DEVICE, UsbDevice::class.java)
                        android.util.Log.i("MainActivity", "App launched by USB device: ${device?.deviceName}")
                        device?.let {
                            vm.handleUsbDeviceAttached(it)
                        }
                    }
                }

                val screen by vm.screen.collectAsState()
                val rosStarted by vm.rosStarted.collectAsState()
                val rosDomainId by vm.rosDomainId.collectAsState()
                val deviceId by vm.deviceId.collectAsState()
                val sensors by vm.sensors.collectAsState()
                val cameras by vm.cameras.collectAsState()
                val externalDevices by vm.externalDevices.collectAsState()
                val reading by vm.currentReading.collectAsState()
                val networkInterfaces by vm.networkInterfaces.collectAsState()
                val selectedNetworkInterface by vm.selectedNetworkInterface.collectAsState()
                val pipelineNodes by vm.pipelineNodes.collectAsState()
                val nodeStates by vm.nodeStates.collectAsState()
                val cameraFrame by vm.cameraFrame.collectAsState()
                val activeNotifications by vm.notifications.collectAsState()
                val perceptionState by vm.perceptionState.collectAsState()
                val debugFrameRgb by vm.debugFrameRgb.collectAsState()
                val beetlePredatorState by vm.beetlePredatorState.collectAsState()
                val beetlePredatorDebugFrame by vm.beetlePredatorDebugFrame.collectAsState()

                // Handle orientation and immersive mode based on screen
                LaunchedEffect(screen) {
                    val insetsController = WindowCompat.getInsetsController(window, window.decorView)
                    if (screen is Screen.DebugVisualizationFullscreen) {
                        requestedOrientation = ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE
                        insetsController.hide(WindowInsetsCompat.Type.systemBars())
                        insetsController.systemBarsBehavior =
                            WindowInsetsControllerCompat.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
                    } else {
                        requestedOrientation = ActivityInfo.SCREEN_ORIENTATION_PORTRAIT
                        insetsController.show(WindowInsetsCompat.Type.systemBars())
                    }
                }

                // Handle back button: navigate back if in submenu, exit if on Dashboard
                BackHandler(enabled = screen != Screen.Dashboard) {
                    vm.navigateBack()
                }

                // Also handle back button for fullscreen (ensures exit from fullscreen)
                BackHandler(enabled = screen == Screen.DebugVisualizationFullscreen) {
                    vm.navigateBack()
                }

                Box(modifier = Modifier.fillMaxSize()) {
                when (val s = screen) {
                    is Screen.Dashboard -> DashboardScreen(
                        rosStarted = rosStarted,
                        rosDomainId = rosDomainId,
                        sensorCount = sensors.size,
                        cameraCount = cameras.size,
                        externalDeviceCount = externalDevices.size,
                        onSettingsClick = { vm.navigateToRosSetup() },
                        onBuiltInSensorsClick = { vm.navigateToBuiltInSensors() },
                        onExternalSensorsClick = { vm.navigateToExternalSensors() },
                        onSubsystemClick = { vm.navigateToSubsystem() },
                        onBeetlePredatorClick = { vm.navigateToBeetlePredator() }
                    )
                    is Screen.RosSetup -> RosSetupScreen(
                        rosStarted = rosStarted,
                        networkInterfaces = networkInterfaces,
                        rosDomainId = rosDomainId,
                        deviceId = deviceId,
                        selectedNetworkInterface = selectedNetworkInterface,
                        onBack = { vm.navigateBack() },
                        onStartRos = { domainId, iface, devId -> vm.startRos(domainId, iface, devId) },
                        onResetRos = { vm.resetRos() },
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
                    is Screen.ExternalSensors -> ExternalSensorsScreen(
                        devices = externalDevices,
                        onBack = { vm.navigateBack() },
                        onScanDevices = { vm.scanForExternalDevices() },
                        onLidarClick = { vm.navigateToLidar(it) }
                    )
                    is Screen.LidarDetail -> {
                        val device = externalDevices.find { it.uniqueId == s.deviceId }
                        val devicesBeingToggled by vm.devicesBeingToggled.collectAsState()
                        if (device != null) {
                            LidarDetailScreen(
                                device = device,
                                isBeingToggled = devicesBeingToggled.contains(device.uniqueId),
                                onBack = { vm.navigateBack() },
                                onConnect = { vm.connectLidar(device.uniqueId) },
                                onDisconnect = { vm.disconnectLidar(device.uniqueId) },
                                onEnable = { vm.enableLidar(device.uniqueId) },
                                onDisable = { vm.disableLidar(device.uniqueId) },
                                onBaudrateChange = { baudrate -> vm.updateLidarBaudrate(device.uniqueId, baudrate) }
                            )
                        }
                    }
                    is Screen.UsbCameraDetail -> {
                        // TODO: USB camera detail screen - Phase 2+
                        Text("USB Camera Detail - Coming Soon")
                    }
                    is Screen.Subsystem -> SubsystemScreen(
                        nodes = pipelineNodes,
                        onBack = { vm.navigateBack() },
                        onNodeClick = { vm.navigateToNode(it) },
                        onNodeStartStop = { vm.toggleNodeState(it) },
                        isNodeStartable = { vm.isNodeStartable(it) },
                        canProbeNode = { vm.canProbeNode(it) },
                        isNodeProbing = { nodeStates[it]?.isProbing == true },
                        onToggleNodeProbing = { vm.toggleNodeProbing(it) },
                        onReset = { vm.resetPipeline() },
                        isRunningLocally = { nodeStates[it]?.runningLocally == true },
                        isDetectedOnNetwork = { nodeStates[it]?.detectedOnNetwork == true },
                        isNodeStarting = { nodeStates[it]?.isStarting == true }
                    )
                    is Screen.NodeDetail -> {
                        val node = pipelineNodes.find { it.id == s.nodeId }
                        if (node != null) {
                            NodeDetailScreen(
                                node = node,
                                canStart = vm.isNodeStartable(node.id),
                                onBack = { vm.navigateBack() },
                                onStartStop = { vm.toggleNodeState(node.id) },
                                runningLocally = vm.isNodeRunningLocally(node.id),
                                detectedOnNetwork = vm.isNodeDetectedOnNetwork(node.id),
                                visualizationEnabled = perceptionState.visualizationEnabled,
                                debugFrameRgb = debugFrameRgb,
                                onEnableVisualization = { vm.enableVisualization() },
                                onDisableVisualization = { vm.disableVisualization() },
                                onFullscreenClick = { vm.navigateToDebugFullscreen() },
                                isNodeStarting = { nodeStates[it]?.isStarting == true }
                            )
                        }
                    }
                    is Screen.DebugVisualizationFullscreen -> {
                        FullscreenDebugVisualizationScreen(
                            debugFrameRgb = debugFrameRgb,
                            onBack = { vm.navigateBack() }
                        )
                    }
                    is Screen.BeetlePredator -> {
                        BeetlePredatorScreen(
                            state = beetlePredatorState,
                            debugFrame = beetlePredatorDebugFrame,
                            onBack = { vm.navigateBack() },
                            onEnable = { vm.enableBeetlePredator() },
                            onDisable = { vm.disableBeetlePredator() },
                            onToggleLabel = { label -> vm.toggleBeetlePredatorLabel(label) }
                        )
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
