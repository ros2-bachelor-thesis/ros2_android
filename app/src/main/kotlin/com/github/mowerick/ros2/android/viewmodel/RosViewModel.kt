package com.github.mowerick.ros2.android.viewmodel

import android.content.Context
import android.content.Intent
import android.graphics.Bitmap
import android.net.wifi.WifiManager
import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.github.mowerick.ros2.android.GpsManager
import com.github.mowerick.ros2.android.MainActivity
import com.github.mowerick.ros2.android.NativeBridge
import com.github.mowerick.ros2.android.UsbDeviceManager
import com.github.mowerick.ros2.android.interfaces.NetworkInterfaceProvider
import com.github.mowerick.ros2.android.interfaces.PermissionHandler
import com.github.mowerick.ros2.android.model.CameraInfo
import com.github.mowerick.ros2.android.model.ExternalDeviceInfo
import com.github.mowerick.ros2.android.model.NativeNotification
import com.github.mowerick.ros2.android.model.NodeDependencyGraph
import com.github.mowerick.ros2.android.model.NodeState
import com.github.mowerick.ros2.android.model.PipelineNode
import com.github.mowerick.ros2.android.model.SensorInfo
import com.github.mowerick.ros2.android.model.SensorReading
import com.github.mowerick.ros2.android.model.SensorType
import com.github.mowerick.ros2.android.model.Severity
import com.github.mowerick.ros2.android.model.TopicInfo
import com.github.mowerick.ros2.android.util.getDefaultDeviceId
import com.github.mowerick.ros2.android.util.sanitizeDeviceId
import com.jakewharton.processphoenix.ProcessPhoenix
import java.net.NetworkInterface
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
}

class RosViewModel(
    private val applicationContext: Context,
    private val permissionHandler: PermissionHandler,
    private val networkProvider: NetworkInterfaceProvider,
    initialScreen: Screen = Screen.Dashboard
) : ViewModel() {

    private val _screen = MutableStateFlow<Screen>(initialScreen)
    val screen: StateFlow<Screen> = _screen

    private val _rosStarted = MutableStateFlow(false)
    val rosStarted: StateFlow<Boolean> = _rosStarted

    private val _rosDomainId = MutableStateFlow(-1)
    val rosDomainId: StateFlow<Int> = _rosDomainId

    private val _deviceId = MutableStateFlow(getDefaultDeviceId(applicationContext))
    val deviceId: StateFlow<String> = _deviceId

    private var multicastLock: WifiManager.MulticastLock? = null
    private val gpsManager = GpsManager(applicationContext)
    private val usbDeviceManager = UsbDeviceManager(applicationContext)

    private val _sensors = MutableStateFlow<List<SensorInfo>>(emptyList())
    val sensors: StateFlow<List<SensorInfo>> = _sensors

    private val _cameras = MutableStateFlow<List<CameraInfo>>(emptyList())
    val cameras: StateFlow<List<CameraInfo>> = _cameras

    private val _externalDevices = MutableStateFlow<List<ExternalDeviceInfo>>(emptyList())
    val externalDevices: StateFlow<List<ExternalDeviceInfo>> = _externalDevices

    private val _currentReading = MutableStateFlow<SensorReading?>(null)
    val currentReading: StateFlow<SensorReading?> = _currentReading

    // Track location service state for GPS status display
    private var isLocationServiceEnabled = true

    private val _networkInterfaces = MutableStateFlow<List<String>>(emptyList())
    val networkInterfaces: StateFlow<List<String>> = _networkInterfaces

    private val _selectedNetworkInterface = MutableStateFlow<String?>(null)
    val selectedNetworkInterface: StateFlow<String?> = _selectedNetworkInterface

    private val _pipelineNodes = MutableStateFlow(createDefaultPipelineNodes())
    val pipelineNodes: StateFlow<List<PipelineNode>> = _pipelineNodes

    private val _discoveredTopics = MutableStateFlow<Set<String>>(emptySet())

    private val _cameraFrame = MutableStateFlow<Bitmap?>(null)
    val cameraFrame: StateFlow<Bitmap?> = _cameraFrame

    private val _notifications = MutableStateFlow<List<NativeNotification>>(emptyList())
    val notifications: StateFlow<List<NativeNotification>> = _notifications

    private var nextNotificationId = 0L

    private val _isProbing = MutableStateFlow(false)
    val isProbing: StateFlow<Boolean> = _isProbing

    init {
        // Register notification callback instead of polling
        NativeBridge.setNotificationCallback { severity, message ->
            viewModelScope.launch {
                addNotification(message, if (severity == "ERROR") Severity.ERROR else Severity.WARNING)
            }
        }

        // Register sensor data callback (replaces polling)
        NativeBridge.setSensorDataCallback { sensorId ->
            viewModelScope.launch {
                // Only update if we're viewing this sensor's detail screen
                val currentScreen = _screen.value
                if (currentScreen is Screen.SensorDetail && currentScreen.sensorId == sensorId) {
                    try {
                        var reading = NativeBridge.nativeGetSensorData(sensorId)

                        // Override GPS reading with status message if location services disabled
                        if (sensorId == "gps_location_provider" && !isLocationServiceEnabled) {
                            reading = SensorReading(
                                values = emptyList(),
                                unit = "",
                                sensorType = SensorType.GPS,
                                statusMessage = "Location services disabled"
                            )
                        }

                        _currentReading.value = reading
                    } catch (e: Exception) {
                        android.util.Log.e("RosViewModel", "Failed to get sensor data for $sensorId", e)
                    }
                }
            }
        }

        // Register camera frame callback (replaces polling)
        NativeBridge.setCameraFrameCallback { cameraId ->
            viewModelScope.launch {
                // Only update if we're viewing this camera's detail screen
                val currentScreen = _screen.value
                if (currentScreen is Screen.CameraDetail && currentScreen.cameraId == cameraId) {
                    try {
                        val bitmap = NativeBridge.nativeGetCameraFrame(cameraId)
                        _cameraFrame.value = bitmap
                    } catch (e: Exception) {
                        android.util.Log.e("RosViewModel", "Failed to get camera frame for $cameraId", e)
                    }
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
                android.util.Log.w("RosViewModel", "Location services disabled outside app")
                isLocationServiceEnabled = false
                // Disable GPS sensor when location services are disabled
                NativeBridge.nativeDisableSensor("gps_location_provider")
                refreshSensors()
                refreshCurrentReading()
            },
            onLocationServiceEnabled = {
                android.util.Log.i("RosViewModel", "Location services re-enabled outside app")
                isLocationServiceEnabled = true
                // Refresh GPS sensor reading to clear status message
                refreshCurrentReading()
            }
        )

        // Register GPS enable callback from native - when sensor is enabled, ensure GPS is running
        NativeBridge.setGpsCallbacks(
            onEnable = {
                android.util.Log.i("RosViewModel", "GPS sensor enabled - ensuring GPS manager is running")
                if (!gpsManager.isRunning()) {
                    val launcher = permissionHandler.getLocationSettingsLauncher()
                    gpsManager.startWithChecks(launcher)
                }
            },
            onDisable = {
                android.util.Log.i("RosViewModel", "GPS sensor disabled - publishing stopped (GPS manager keeps running for display)")
                // Don't stop GPS manager - keep it running for data display
            }
        )
    }

    private fun addNotification(message: String, severity: Severity) {
        val now = System.currentTimeMillis()
        val notification = NativeNotification(
            id = nextNotificationId++,
            message = message,
            severity = severity,
            timestampMs = now
        )
        // Combine with existing non-expired, cap visible list
        val existing = _notifications.value.filter { now - it.timestampMs < 5000 }
        _notifications.value = (existing + notification).takeLast(50)

        // Schedule auto-dismiss for this notification after 5 seconds
        viewModelScope.launch {
            delay(5000)
            _notifications.value = _notifications.value.filter { it.id != notification.id }
        }
    }

    fun dismissNotification(id: Long) {
        _notifications.value = _notifications.value.filter { it.id != id }
    }

    fun loadNetworkInterfaces() {
        try {
            val ifaces = NativeBridge.nativeGetNetworkInterfaces()
            _networkInterfaces.value = ifaces.toList()
        } catch (e: Exception) {
            android.util.Log.e("RosViewModel", "Failed to load network interfaces", e)
        }
    }

    fun refreshNetworkInterfaces() {
        try {
            val ifaces = networkProvider.queryNetworkInterfaces()
            NativeBridge.nativeSetNetworkInterfaces(ifaces)
            loadNetworkInterfaces()
        } catch (e: Exception) {
            android.util.Log.e("RosViewModel", "Failed to refresh network interfaces", e)
        }
    }

    fun setDomainId(id: Int) {
        _rosDomainId.value = id
    }

    fun setDeviceId(id: String) {
        _deviceId.value = id
    }

    fun startRos(domainId: Int, networkInterface: String, deviceId: String) {
        // Acquire multicast lock for DDS discovery
        acquireMulticastLock()

        val sanitizedDeviceId = sanitizeDeviceId(deviceId)
        _deviceId.value = sanitizedDeviceId
        NativeBridge.nativeStartRos(domainId, networkInterface, sanitizedDeviceId)
        _rosDomainId.value = domainId
        _selectedNetworkInterface.value = networkInterface
        _rosStarted.value = true
        refreshSensorsAndCameras()
    }

    fun resetRos() {
        android.util.Log.i("RosViewModel", "Resetting ROS - restarting app")

        // Release multicast lock before restart
        releaseMulticastLock()

        // Stop GPS manager
        gpsManager.stop()

        // Source - https://stackoverflow.com/a/74613696
        // Posted by TIMBLOCKER, modified by community. See post 'Timeline' for change history
        // Retrieved 2026-03-19, License - CC BY-SA 4.0
        // Restart the app and navigate to settings screen using ProcessPhoenix
        val intent = Intent(applicationContext, MainActivity::class.java).apply {
            putExtra("navigate_to_settings", true)
        }
        ProcessPhoenix.triggerRebirth(applicationContext, intent)
    }

    fun onLocationSettingsEnabled() {
        android.util.Log.i("RosViewModel", "User enabled location settings, restarting GPS")
        // GPS manager will handle starting location updates
        val launcher = permissionHandler.getLocationSettingsLauncher()
        gpsManager.startWithChecks(launcher)
    }

    fun onLocationSettingsCancelled() {
        android.util.Log.w("RosViewModel", "User cancelled location settings dialog")
        addNotification("GPS: Location services required", Severity.WARNING)
    }

    private fun acquireMulticastLock() {
        try {
            val wifiManager = applicationContext.getSystemService(Context.WIFI_SERVICE) as WifiManager
            multicastLock = wifiManager.createMulticastLock("ros2_dds_discovery").apply {
                setReferenceCounted(false)
                acquire()
            }
        } catch (e: Exception) {
            // Log or notify error
        }
    }

    private fun releaseMulticastLock() {
        try {
            multicastLock?.let {
                if (it.isHeld) {
                    it.release()
                }
            }
            multicastLock = null
        } catch (e: Exception) {
            android.util.Log.e("RosViewModel", "Failed to release multicast lock", e)
        }
    }

    /**
     * Refresh the current sensor reading (used when location service state changes)
     */
    private fun refreshCurrentReading() {
        val currentScreen = _screen.value
        if (currentScreen is Screen.SensorDetail) {
            viewModelScope.launch {
                try {
                    var reading = NativeBridge.nativeGetSensorData(currentScreen.sensorId)

                    // Override GPS reading with status message if location services disabled
                    if (currentScreen.sensorId == "gps_location_provider" && !isLocationServiceEnabled) {
                        reading = SensorReading(
                            values = emptyList(),
                            unit = "",
                            sensorType = SensorType.GPS,
                            statusMessage = "Location services disabled"
                        )
                    }

                    _currentReading.value = reading
                } catch (e: Exception) {
                    android.util.Log.e("RosViewModel", "Failed to refresh sensor data", e)
                }
            }
        }
    }

    override fun onCleared() {
        super.onCleared()
        releaseMulticastLock()
        usbDeviceManager.destroy()
    }

    // -- Dashboard navigation --

    fun navigateToRosSetup() {
        _screen.value = Screen.RosSetup
    }

    fun navigateToBuiltInSensors() {
        refreshSensorsAndCameras()
        _screen.value = Screen.BuiltInSensors
    }

    fun navigateToExternalSensors() {
        refreshExternalDevices()
        _screen.value = Screen.ExternalSensors
    }

    fun navigateToSubsystem() {
        _screen.value = Screen.Subsystem
    }

    // -- Sensor/Camera navigation --

    fun navigateToSensor(sensor: SensorInfo) {
        _currentReading.value = null
        _screen.value = Screen.SensorDetail(sensor.uniqueId)
        // Callback will handle updates automatically
    }

    fun navigateToCamera(camera: CameraInfo) {
        _screen.value = Screen.CameraDetail(camera.uniqueId)
        // Callback will handle frame updates automatically
    }

    // -- External device navigation --

    fun navigateToLidar(device: ExternalDeviceInfo) {
        _screen.value = Screen.LidarDetail(device.uniqueId)
    }

    // -- Pipeline node navigation --

    fun navigateToNode(node: PipelineNode) {
        _screen.value = Screen.NodeDetail(node.id)
    }

    fun isNodeStartable(nodeId: String): Boolean {
        val graph = NodeDependencyGraph(_pipelineNodes.value)
        return graph.isNodeStartable(nodeId)
    }

    fun toggleNodeState(nodeId: String) {
        val graph = NodeDependencyGraph(_pipelineNodes.value)
        _pipelineNodes.value = graph.toggleNodeState(nodeId)
    }

    // -- Back navigation --

    fun navigateBack() {
        // Clear sensor reading and camera frame when navigating away
        when (_screen.value) {
            is Screen.SensorDetail, is Screen.CameraDetail -> {
                _currentReading.value = null  // Clear sensor reading
                _cameraFrame.value = null     // Clear camera frame
                refreshSensorsAndCameras()
                _screen.value = Screen.BuiltInSensors
            }
            is Screen.LidarDetail, is Screen.UsbCameraDetail -> {
                refreshExternalDevices()
                _screen.value = Screen.ExternalSensors
            }
            is Screen.BuiltInSensors, is Screen.ExternalSensors, is Screen.Subsystem, is Screen.RosSetup -> {
                _screen.value = Screen.Dashboard
            }
            is Screen.NodeDetail -> {
                _screen.value = Screen.Subsystem
            }
            else -> {}
        }
    }

    // -- Sensor/Camera enable/disable --

    fun enableCamera(uniqueId: String) {
        NativeBridge.nativeEnableCamera(uniqueId)
        refreshCameras()
        // Callback will handle frame updates automatically
    }

    fun disableCamera(uniqueId: String) {
        _cameraFrame.value = null  // Clear camera frame
        NativeBridge.nativeDisableCamera(uniqueId)
        refreshCameras()
    }

    fun enableSensor(uniqueId: String) {
        // Enable sensor publishing only - data collection is always active
        NativeBridge.nativeEnableSensor(uniqueId)
        refreshSensors()
    }

    fun disableSensor(uniqueId: String) {
        NativeBridge.nativeDisableSensor(uniqueId)
        refreshSensors()
    }

    fun onLocationPermissionGranted() {
        android.util.Log.i("RosViewModel", "Location permission granted")
        if (_rosStarted.value && !gpsManager.isRunning()) {
            val launcher = permissionHandler.getLocationSettingsLauncher()
            gpsManager.startWithChecks(launcher)
        }
    }

    fun onLocationPermissionDenied() {
        android.util.Log.w("RosViewModel", "Location permission denied by user")
        addNotification("GPS: Location permission required", Severity.WARNING)
    }

    // -- External device control --

    fun connectLidar(uniqueId: String) {
        // Find the UsbDevice from detected devices
        val usbDevice = usbDeviceManager.detectLidarDevices().find {
            usbDeviceManager.deviceToInfo(it).uniqueId == uniqueId
        } ?: run {
            addNotification("LIDAR device not found: $uniqueId", Severity.ERROR)
            return
        }

        // Request permission if needed
        usbDeviceManager.requestPermission(usbDevice) { granted ->
            if (granted) {
                // Open device and get file descriptor
                val fdAndPath = usbDeviceManager.openDevice(usbDevice)
                if (fdAndPath != null) {
                    val (fd, path) = fdAndPath
                    android.util.Log.i("RosViewModel", "USB device opened: fd=$fd, path=$path")

                    // Pass FD to native layer
                    val success = NativeBridge.nativeConnectLidar(fd, path, uniqueId)
                    if (success) {
                        addNotification("LIDAR connected: $path", Severity.WARNING)
                        refreshExternalDevices()
                    } else {
                        addNotification("Failed to initialize LIDAR in native layer", Severity.ERROR)
                        usbDeviceManager.closeDevice()
                    }
                } else {
                    addNotification("Failed to open LIDAR device", Severity.ERROR)
                }
            } else {
                addNotification("USB permission denied for LIDAR", Severity.ERROR)
            }
        }
    }

    fun disconnectLidar(uniqueId: String) {
        val success = NativeBridge.nativeDisconnectLidar(uniqueId)
        if (success) {
            usbDeviceManager.closeDevice()
            addNotification("LIDAR disconnected", Severity.WARNING)
            refreshExternalDevices()
        } else {
            addNotification("Failed to disconnect LIDAR", Severity.ERROR)
        }
    }

    fun enableLidar(uniqueId: String) {
        val success = NativeBridge.nativeEnableLidar(uniqueId)
        if (success) {
            android.util.Log.i("RosViewModel", "LIDAR publishing enabled: $uniqueId")
            addNotification("LIDAR publishing enabled", Severity.WARNING)
            refreshExternalDevices()
        } else {
            addNotification("Failed to enable LIDAR publishing", Severity.ERROR)
        }
    }

    fun disableLidar(uniqueId: String) {
        val success = NativeBridge.nativeDisableLidar(uniqueId)
        if (success) {
            android.util.Log.i("RosViewModel", "LIDAR publishing disabled: $uniqueId")
            addNotification("LIDAR publishing disabled", Severity.WARNING)
            refreshExternalDevices()
        } else {
            addNotification("Failed to disable LIDAR publishing", Severity.ERROR)
        }
    }

    // -- Private helpers --

    private fun refreshSensorsAndCameras() {
        refreshSensors()
        refreshCameras()
    }

    private fun refreshSensors() {
        try {
            val sensors = NativeBridge.nativeGetSensorList()
            _sensors.value = sensors.toList()
        } catch (e: Exception) {
            android.util.Log.e("RosViewModel", "Failed to refresh sensors", e)
        }
    }

    private fun refreshExternalDevices() {
        // Detect USB LIDAR devices
        val lidarDevices = usbDeviceManager.detectLidarDevices()
        _externalDevices.value = lidarDevices.map { device ->
            usbDeviceManager.deviceToInfo(
                device = device,
                connected = usbDeviceManager.hasPermission(device),
                enabled = false  // TODO: Phase 3 - query native layer for enabled status
            )
        }
    }

    private fun refreshCameras() {
        try{
            val cameras = NativeBridge.nativeGetCameraList()
            _cameras.value = cameras.toList()
        } catch (e: Exception) {
            android.util.Log.e("RosViewModel", "Failed to refresh cameras", e)
        }
    }

    fun toggleTopicProbing() {
        _isProbing.value = !_isProbing.value
        if (_isProbing.value) {
            viewModelScope.launch {
                while (_isProbing.value) {
                    try {
                        val topics = NativeBridge.nativeGetDiscoveredTopics()
                        _discoveredTopics.value = topics.toSet()
                        updateExternalNodeStates(topics.toSet())
                    } catch (_: UnsatisfiedLinkError) {
                        _isProbing.value = false
                        return@launch
                    } catch (e: Exception) {
                        android.util.Log.e("RosViewModel", "Failed to probe topics", e)
                    }
                    delay(2000)
                }
            }
        }
    }

    private fun updateExternalNodeStates(discoveredTopics: Set<String>) {
        val graph = NodeDependencyGraph(_pipelineNodes.value)
        val (updated, changed) = graph.updateExternalNodeStates(discoveredTopics)

        if (changed) {
            _pipelineNodes.value = updated
            // If an external node went to Stopped, cascade stop dependents
            val stoppedExternals = updated.filter { it.isExternal && it.state == NodeState.Stopped }
            for (ext in stoppedExternals) {
                val cascadeGraph = NodeDependencyGraph(_pipelineNodes.value)
                _pipelineNodes.value = cascadeGraph.cascadeStop(ext.id)
            }
        }
    }


    companion object {
        private fun createDefaultPipelineNodes(): List<PipelineNode> = listOf(
            PipelineNode(
                id = "zed_stereo_node",
                name = "ZED 2i Camera",
                description = "Captures stereo image, depth, point cloud, and IMU data from ZED 2i camera. Runs on external NVIDIA Jetson/PC and streams to Android via DDS.",
                state = NodeState.Stopped,
                subscribesTo = emptyList(),
                publishesTo = listOf(
                    TopicInfo("/zed/zed_node/rgb/image_rect_color/compressed", "sensor_msgs/msg/CompressedImage"),
                    TopicInfo("/zed/zed_node/depth/depth_registered", "sensor_msgs/msg/Image"),
                    TopicInfo("/zed/zed_node/point_cloud/cloud_registered", "sensor_msgs/msg/PointCloud2"),
                    TopicInfo("/zed/zed_node/imu/data", "sensor_msgs/msg/Imu")
                ),
                upstreamNodeId = null,
                isExternal = true
            ),
            PipelineNode(
                id = "object_detection",
                name = "YOLOv9 Object Detection",
                description = "Runs YOLOv9 + Deep SORT to detect and track Colorado Potato Beetle life stages (beetle, larva, eggs) in 3D space using ZED camera data.",
                state = NodeState.Stopped,
                subscribesTo = listOf(
                    TopicInfo("/zed/zed_node/rgb/image_rect_color/compressed", "sensor_msgs/msg/CompressedImage"),
                    TopicInfo("/zed/zed_node/depth/depth_registered", "sensor_msgs/msg/Image"),
                    TopicInfo("/zed/zed_node/point_cloud/cloud_registered", "sensor_msgs/msg/PointCloud2")
                ),
                publishesTo = listOf(
                    TopicInfo("/cpb_beetle_center", "geometry_msgs/msg/Point"),
                    TopicInfo("/cpb_larva_center", "geometry_msgs/msg/Point"),
                    TopicInfo("/cpb_eggs_center", "geometry_msgs/msg/Point"),
                    TopicInfo("/cpb_beetle", "sensor_msgs/msg/PointCloud2"),
                    TopicInfo("/cpb_larva", "sensor_msgs/msg/PointCloud2"),
                    TopicInfo("/cpb_eggs", "sensor_msgs/msg/PointCloud2")
                ),
                upstreamNodeId = "zed_stereo_node",
                isExternal = false
            ),
            PipelineNode(
                id = "target_manager",
                name = "Target Manager",
                description = "Selects primary targets (CPB eggs) and performs IMU-based orientation calibration for laser positioning. Computes pan/tilt commands with offset correction.",
                state = NodeState.Stopped,
                subscribesTo = listOf(
                    TopicInfo("/cpb_eggs_center", "geometry_msgs/msg/Point"),
                    TopicInfo("/zed/zed_node/imu/data", "sensor_msgs/msg/Imu")
                ),
                publishesTo = listOf(
                    TopicInfo("/arm_position_goal", "std_msgs/msg/Float32MultiArray")
                ),
                upstreamNodeId = "object_detection",
                isExternal = false
            ),
            PipelineNode(
                id = "arm_commander",
                name = "Arm Commander",
                description = "State machine for pan/tilt arm control with ACK/NACK protocol. Manages command retries, timeouts, and feedback synchronization with microcontroller.",
                state = NodeState.Stopped,
                subscribesTo = listOf(
                    TopicInfo("/arm_position_goal", "std_msgs/msg/Float32MultiArray"),
                    TopicInfo("/PointNShoot_ACK", "std_msgs/msg/Float32"),
                    TopicInfo("/PointNShoot_DONE", "std_msgs/msg/Float32"),
                    TopicInfo("/PointNShoot_NACK", "std_msgs/msg/Float32")
                ),
                publishesTo = listOf(
                    TopicInfo("/PointNShoot", "std_msgs/msg/Float32MultiArray"),
                    TopicInfo("/arm_position_feedback", "std_msgs/msg/String")
                ),
                upstreamNodeId = "target_manager",
                isExternal = false
            ),
            PipelineNode(
                id = "micro_ros_agent",
                name = "micro-ROS Agent",
                description = "Bridges ROS 2 DDS network to Zephyr microcontroller via USB serial (921600 baud). Forwards /PointNShoot commands to pan/tilt arm MCU. Investigation - may require external PC.",
                state = NodeState.Stopped,
                subscribesTo = listOf(
                    TopicInfo("/PointNShoot", "std_msgs/msg/Float32MultiArray")
                ),
                publishesTo = emptyList(),
                upstreamNodeId = "arm_commander",
                isExternal = false
            )
        )
    }

}
