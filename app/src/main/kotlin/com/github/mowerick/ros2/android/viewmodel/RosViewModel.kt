package com.github.mowerick.ros2.android.viewmodel

import android.content.Context
import android.content.Intent
import android.graphics.Bitmap
import android.hardware.usb.UsbDevice
import android.util.Log
import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.github.mowerick.ros2.android.viewmodel.managers.GpsManager
import com.github.mowerick.ros2.android.MainActivity
import com.github.mowerick.ros2.android.util.NativeBridge
import com.github.mowerick.ros2.android.viewmodel.managers.UsbSerialManager
import com.github.mowerick.ros2.android.util.UsbSerialBridge
import com.github.mowerick.ros2.android.interfaces.NetworkInterfaceProvider
import com.github.mowerick.ros2.android.interfaces.PermissionHandler
import com.github.mowerick.ros2.android.model.CameraInfo
import com.github.mowerick.ros2.android.model.ExternalDeviceInfo
import com.github.mowerick.ros2.android.model.NativeNotification
import com.github.mowerick.ros2.android.model.NodeRuntimeState
import com.github.mowerick.ros2.android.model.NodeState
import com.github.mowerick.ros2.android.model.PipelineNode
import com.github.mowerick.ros2.android.model.PipelineState
import com.github.mowerick.ros2.android.model.SensorInfo
import com.github.mowerick.ros2.android.model.SensorReading
import com.github.mowerick.ros2.android.model.Severity
import com.github.mowerick.ros2.android.viewmodel.managers.BeetlePredatorManager
import com.github.mowerick.ros2.android.viewmodel.managers.ExternalDeviceManager
import com.github.mowerick.ros2.android.viewmodel.managers.NavigationManager
import com.github.mowerick.ros2.android.viewmodel.managers.PerceptionManager
import com.github.mowerick.ros2.android.viewmodel.managers.PipelineStateMachine
import com.github.mowerick.ros2.android.viewmodel.managers.RosLifecycleManager
import com.github.mowerick.ros2.android.viewmodel.managers.SensorCameraManager
import com.jakewharton.processphoenix.ProcessPhoenix
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.launch

sealed class Screen {
    data object Dashboard : Screen()
    data object RosSetup : Screen()
    data object BuiltInSensors : Screen()
    data object ExternalSensors : Screen()
    data object Subsystem : Screen()
    data class SensorDetail(val sensorId: String) : Screen()
    data class CameraDetail(val cameraId: String) : Screen()
    data class LidarDetail(val deviceId: String) : Screen()
    data class UsbCameraDetail(val deviceId: String) : Screen()
    data class NodeDetail(val nodeId: String) : Screen()
    data object CommandBridge : Screen()
    data object DebugVisualizationFullscreen : Screen()
    data object BeetlePredator : Screen()
}

class RosViewModel(
    private val applicationContext: Context,
    private val permissionHandler: PermissionHandler,
    networkProvider: NetworkInterfaceProvider,
    initialScreen: Screen = Screen.Dashboard
) : ViewModel() {

    // Managers
    private val navigationManager = NavigationManager(initialScreen)
    private val rosLifecycleManager = RosLifecycleManager(applicationContext, networkProvider)
    private val sensorCameraManager = SensorCameraManager(viewModelScope) { isLocationServiceEnabled }
    private val usbSerialManager = UsbSerialManager(applicationContext)
    private val externalDeviceManager = ExternalDeviceManager(
        usbSerialManager,
        viewModelScope
    ) { message -> addNotification(message, Severity.WARNING) }
    private val perceptionManager = PerceptionManager(
        applicationContext,
        viewModelScope
    ) { message -> addNotification(message, Severity.ERROR) }
    private val pipelineStateMachine = PipelineStateMachine(
        applicationContext,
        viewModelScope,
        { message -> addNotification(message, Severity.ERROR) },
        { enabled -> perceptionManager.setEnabled(enabled) }
    )

    // GPS Manager (stays here - too coupled with permissions/settings)
    private val gpsManager = GpsManager(applicationContext)
    private var isLocationServiceEnabled = true

    // Beetle Predator
    private val beetlePredatorManager = BeetlePredatorManager(
        applicationContext,
        viewModelScope,
        gpsManager,
        { permissionHandler.getLocationSettingsLauncher() },
        { message -> addNotification(message, Severity.ERROR) }
    )

    // Notifications (stays here - central coordination point)
    private val _notifications = MutableStateFlow<List<NativeNotification>>(emptyList())
    val notifications: StateFlow<List<NativeNotification>> = _notifications
    private var nextNotificationId = 0L

    // Expose manager state flows
    val screen: StateFlow<Screen> = navigationManager.screen
    val rosStarted: StateFlow<Boolean> = rosLifecycleManager.rosStarted
    val rosDomainId: StateFlow<Int> = rosLifecycleManager.rosDomainId
    val deviceId: StateFlow<String> = rosLifecycleManager.deviceId
    val networkInterfaces: StateFlow<List<String>> = rosLifecycleManager.networkInterfaces
    val selectedNetworkInterface: StateFlow<String?> = rosLifecycleManager.selectedNetworkInterface
    val sensors: StateFlow<List<SensorInfo>> = sensorCameraManager.sensors
    val cameras: StateFlow<List<CameraInfo>> = sensorCameraManager.cameras
    val currentReading: StateFlow<SensorReading?> = sensorCameraManager.currentReading
    val cameraFrame: StateFlow<Bitmap?> = sensorCameraManager.cameraFrame
    val externalDevices: StateFlow<List<ExternalDeviceInfo>> = externalDeviceManager.externalDevices
    val devicesBeingToggled: StateFlow<Set<String>> = externalDeviceManager.devicesBeingToggled
    val pipelineNodes: StateFlow<List<PipelineNode>> = pipelineStateMachine.pipelineNodes
    val nodeStates: StateFlow<Map<String, NodeRuntimeState>> = pipelineStateMachine.nodeStates
    val perceptionState: StateFlow<PerceptionManager.PerceptionState> = perceptionManager.perceptionState
    val debugFrameRgb: StateFlow<Bitmap?> = perceptionManager.debugFrameRgb
    val beetlePredatorState: StateFlow<BeetlePredatorManager.BeetlePredatorState> = beetlePredatorManager.state
    val beetlePredatorDebugFrame: StateFlow<Bitmap?> = beetlePredatorManager.debugFrame

    init {
        // Initialize USB Serial manager for LIDAR communication
        UsbSerialBridge.setUsbSerialManager(usbSerialManager)

        // Register notification callback
        NativeBridge.setNotificationCallback { severity, message ->
            viewModelScope.launch {
                addNotification(message, if (severity == "ERROR") Severity.ERROR else Severity.WARNING)
            }
        }

        // Register sensor data callback
        NativeBridge.setSensorDataCallback { sensorId ->
            viewModelScope.launch {
                val currentScreen = screen.value
                if (currentScreen is Screen.SensorDetail && currentScreen.sensorId == sensorId) {
                    sensorCameraManager.updateSensorReading(sensorId)
                }
            }
        }

        // Register camera frame callback
        NativeBridge.setCameraFrameCallback { cameraId ->
            viewModelScope.launch {
                val currentScreen = screen.value
                if (currentScreen is Screen.CameraDetail && currentScreen.cameraId == cameraId) {
                    sensorCameraManager.updateCameraFrame(cameraId)
                }
            }
        }

        // Register debug frame callback
        NativeBridge.setDebugFrameCallback { frameId ->
            viewModelScope.launch {
                val currentScreen = screen.value
                if ((currentScreen is Screen.NodeDetail && currentScreen.nodeId == "object_detection") ||
                     currentScreen is Screen.DebugVisualizationFullscreen) {
                    perceptionManager.updateDebugFrame(frameId)
                }
                if (currentScreen is Screen.BeetlePredator && frameId == "beetle_predator_rgb") {
                    beetlePredatorManager.updateDebugFrame()
                }
            }
        }

        // Set up GPS manager callbacks
        gpsManager.setCallbacks(
            onError = { message ->
                viewModelScope.launch {
                    addNotification(message, Severity.ERROR)
                }
            },
            onPermissionNeeded = {
                permissionHandler.requestLocationPermission()
            },
            onSettingsNeeded = { _ ->
                // Settings check will be triggered when needed
            },
            onLocationServiceDisabled = {
                Log.w("RosViewModel", "Location services disabled outside app")
                isLocationServiceEnabled = false
                NativeBridge.nativeDisableSensor("gps_location_provider")
                sensorCameraManager.refreshSensors()
                refreshCurrentReading()
            },
            onLocationServiceEnabled = {
                Log.i("RosViewModel", "Location services re-enabled outside app")
                isLocationServiceEnabled = true
                refreshCurrentReading()
            }
        )

        // Register GPS enable callback from native
        NativeBridge.setGpsCallbacks(
            onEnable = {
                Log.i("RosViewModel", "GPS sensor enabled - ensuring GPS manager is running")
                if (!gpsManager.isRunning()) {
                    val launcher = permissionHandler.getLocationSettingsLauncher()
                    gpsManager.startWithChecks(launcher)
                }
            },
            onDisable = {
                Log.i("RosViewModel", "GPS sensor disabled - stopping GPS manager")
                gpsManager.stop()
            }
        )
    }

    // -- Notifications --

    private fun addNotification(message: String, severity: Severity) {
        val now = System.currentTimeMillis()
        val notification = NativeNotification(
            id = nextNotificationId++,
            message = message,
            severity = severity,
            timestampMs = now
        )
        val existing = _notifications.value.filter { now - it.timestampMs < 5000 }
        _notifications.value = (existing + notification).takeLast(50)

        viewModelScope.launch {
            delay(5000)
            _notifications.value = _notifications.value.filter { it.id != notification.id }
        }
    }

    fun dismissNotification(id: Long) {
        _notifications.value = _notifications.value.filter { it.id != id }
    }

    // -- Navigation (delegate to NavigationManager) --

    fun navigateToRosSetup() = navigationManager.navigateToRosSetup()
    fun navigateToBuiltInSensors() {
        sensorCameraManager.refreshSensorsAndCameras()
        navigationManager.navigateToBuiltInSensors()
    }
    fun navigateToExternalSensors() {
        externalDeviceManager.refreshExternalDevices()
        navigationManager.navigateToExternalSensors()
    }
    fun navigateToSubsystem() = navigationManager.navigateToSubsystem()
    fun navigateToSensor(sensor: SensorInfo) {
        sensorCameraManager.clearSensorReading()
        navigationManager.navigateToSensor(sensor)
    }
    fun navigateToCamera(camera: CameraInfo) = navigationManager.navigateToCamera(camera)
    fun navigateToLidar(device: ExternalDeviceInfo) = navigationManager.navigateToLidar(device)
    fun navigateToNode(node: PipelineNode) = navigationManager.navigateToNode(node)
    fun navigateToCommandBridge() = navigationManager.navigateToCommandBridge()
    fun navigateToDebugFullscreen() = navigationManager.navigateToDebugFullscreen()
    fun navigateToBeetlePredator() = navigationManager.navigateToBeetlePredator()

    fun navigateBack() {
        when (screen.value) {
            is Screen.SensorDetail, is Screen.CameraDetail -> {
                sensorCameraManager.clearSensorReading()
                sensorCameraManager.clearCameraFrame()
                sensorCameraManager.refreshSensorsAndCameras()
            }
            is Screen.LidarDetail, is Screen.UsbCameraDetail -> {
                externalDeviceManager.refreshExternalDevices()
            }
            else -> {}
        }
        navigationManager.navigateBack()
    }

    // -- ROS Lifecycle (delegate to RosLifecycleManager) --

    fun loadNetworkInterfaces() = rosLifecycleManager.loadNetworkInterfaces()
    fun refreshNetworkInterfaces() = rosLifecycleManager.refreshNetworkInterfaces()
    fun setDomainId(id: Int) = rosLifecycleManager.setDomainId(id)
    fun setDeviceId(id: String) = rosLifecycleManager.setDeviceId(id)

    fun startRos(domainId: Int, networkInterface: String, deviceId: String) {
        rosLifecycleManager.startRos(domainId, networkInterface, deviceId)
        sensorCameraManager.refreshSensorsAndCameras()
        externalDeviceManager.refreshExternalDevices()
    }

    fun resetRos() {
        Log.i("RosViewModel", "Resetting ROS - restarting app")
        rosLifecycleManager.releaseMulticastLock()
        gpsManager.stop()

        val intent = Intent(applicationContext, MainActivity::class.java).apply {
            putExtra("navigate_to_settings", true)
        }
        ProcessPhoenix.triggerRebirth(applicationContext, intent)
    }

    // -- Sensors/Cameras (delegate to SensorCameraManager) --

    fun enableCamera(uniqueId: String) = sensorCameraManager.enableCamera(uniqueId)
    fun disableCamera(uniqueId: String) = sensorCameraManager.disableCamera(uniqueId)
    fun enableSensor(uniqueId: String) = sensorCameraManager.enableSensor(uniqueId)
    fun disableSensor(uniqueId: String) = sensorCameraManager.disableSensor(uniqueId)

    private fun refreshCurrentReading() {
        val currentScreen = screen.value
        if (currentScreen is Screen.SensorDetail) {
            sensorCameraManager.updateSensorReading(currentScreen.sensorId)
        }
    }

    // -- External Devices (delegate to ExternalDeviceManager) --

    fun updateLidarBaudrate(uniqueId: String, baudrate: Int) =
        externalDeviceManager.updateLidarBaudrate(uniqueId, baudrate)
    fun connectLidar(uniqueId: String) = externalDeviceManager.connectLidar(uniqueId)
    fun disconnectLidar(uniqueId: String) = externalDeviceManager.disconnectLidar(uniqueId)
    fun enableLidar(uniqueId: String) = externalDeviceManager.enableLidar(uniqueId)
    fun disableLidar(uniqueId: String) = externalDeviceManager.disableLidar(uniqueId)
    fun scanForExternalDevices() = externalDeviceManager.scanForExternalDevices()

    fun handleUsbDeviceAttached(device: UsbDevice) {
        externalDeviceManager.handleUsbDeviceAttached(
            device,
            rosStarted.value,
            { navigateToExternalSensors() }
        )
    }


    fun isNodeRunningLocally(nodeId: String) = pipelineStateMachine.isNodeRunningLocally(nodeId)
    fun isNodeDetectedOnNetwork(nodeId: String) = pipelineStateMachine.isNodeDetectedOnNetwork(nodeId)
    fun toggleNodeState(nodeId: String) = pipelineStateMachine.toggleNodeState(nodeId)
    fun isNodeStartable(nodeId: String) = pipelineStateMachine.isNodeStartable(nodeId)
    fun canProbeNode(nodeId: String) = pipelineStateMachine.canProbeNode(nodeId)
    fun toggleNodeProbing(nodeId: String) = pipelineStateMachine.toggleNodeProbing(nodeId)
    fun resetPipeline() = pipelineStateMachine.resetPipeline()

    // -- GPS/Location (stays in ViewModel - tightly coupled) --

    fun onLocationPermissionGranted() {
        Log.i("RosViewModel", "Location permission granted")
        if (rosStarted.value && !gpsManager.isRunning()) {
            val launcher = permissionHandler.getLocationSettingsLauncher()
            gpsManager.startWithChecks(launcher)
        }
    }

    fun onLocationPermissionDenied() {
        Log.w("RosViewModel", "Location permission denied by user")
        addNotification("GPS: Location permission required", Severity.WARNING)
    }

    fun onLocationSettingsEnabled() {
        Log.i("RosViewModel", "User enabled location settings, restarting GPS")
        val launcher = permissionHandler.getLocationSettingsLauncher()
        gpsManager.startWithChecks(launcher)
    }

    fun onLocationSettingsCancelled() {
        Log.w("RosViewModel", "User cancelled location settings dialog")
        addNotification("GPS: Location services required", Severity.WARNING)
    }

    fun enableVisualization() = perceptionManager.enableVisualization()
    fun disableVisualization() = perceptionManager.disableVisualization()

    // -- Beetle Predator --
    fun enableBeetlePredator() = beetlePredatorManager.enable()
    fun disableBeetlePredator() = beetlePredatorManager.disable()
    fun toggleBeetlePredatorLabel(label: String) = beetlePredatorManager.toggleLabel(label)

    override fun onCleared() {
        super.onCleared()
        rosLifecycleManager.releaseMulticastLock()
    }
}
