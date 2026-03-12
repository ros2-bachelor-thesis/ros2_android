package com.github.mowerick.ros2.android.viewmodel

import android.content.Context
import android.graphics.Bitmap
import android.net.wifi.WifiManager
import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.github.mowerick.ros2.android.GpsManager
import com.github.mowerick.ros2.android.NativeBridge
import com.github.mowerick.ros2.android.interfaces.NetworkInterfaceProvider
import com.github.mowerick.ros2.android.interfaces.PermissionHandler
import com.github.mowerick.ros2.android.model.CameraInfo
import com.github.mowerick.ros2.android.model.NativeNotification
import com.github.mowerick.ros2.android.model.NodeDependencyGraph
import com.github.mowerick.ros2.android.model.NodeState
import com.github.mowerick.ros2.android.model.PipelineNode
import com.github.mowerick.ros2.android.model.SensorInfo
import com.github.mowerick.ros2.android.model.SensorReading
import com.github.mowerick.ros2.android.model.Severity
import com.github.mowerick.ros2.android.model.TopicInfo
import java.net.NetworkInterface
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.launch

sealed class Screen {
    data object Dashboard : Screen()
    data object RosSetup : Screen()
    data object BuiltInSensors : Screen()
    data object Subsystem : Screen()
    data class SensorDetail(val sensorId: String) : Screen()
    data class CameraDetail(val cameraId: String) : Screen()
    data class NodeDetail(val nodeId: String) : Screen()
}

class RosViewModel(
    private val applicationContext: Context,
    private val permissionHandler: PermissionHandler,
    private val networkProvider: NetworkInterfaceProvider
) : ViewModel() {

    private val _screen = MutableStateFlow<Screen>(Screen.Dashboard)
    val screen: StateFlow<Screen> = _screen

    private val _rosStarted = MutableStateFlow(false)
    val rosStarted: StateFlow<Boolean> = _rosStarted

    private val _rosDomainId = MutableStateFlow(-1)
    val rosDomainId: StateFlow<Int> = _rosDomainId

    private var multicastLock: WifiManager.MulticastLock? = null
    private val gpsManager = GpsManager(applicationContext)

    private val _sensors = MutableStateFlow<List<SensorInfo>>(emptyList())
    val sensors: StateFlow<List<SensorInfo>> = _sensors

    private val _cameras = MutableStateFlow<List<CameraInfo>>(emptyList())
    val cameras: StateFlow<List<CameraInfo>> = _cameras

    private val _currentReading = MutableStateFlow<SensorReading?>(null)
    val currentReading: StateFlow<SensorReading?> = _currentReading

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

    // Track GPS sensor ID for handling user's location settings decision
    private var pendingGpsSensorId: String? = null

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
                        val reading = NativeBridge.nativeGetSensorData(sensorId)
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
            }
        )

        // Register GPS enable/disable callbacks from native
        NativeBridge.setGpsCallbacks(
            onEnable = {
                android.util.Log.i("RosViewModel", "GPS enable callback received")
                // Store the GPS sensor ID to track this enable request
                pendingGpsSensorId = "gps_location_provider"
                val launcher = permissionHandler.getLocationSettingsLauncher()
                gpsManager.startWithChecks(launcher)
            },
            onDisable = {
                android.util.Log.i("RosViewModel", "GPS disable callback received")
                gpsManager.stop()
                // Clear pending request since GPS is being disabled
                pendingGpsSensorId = null
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

    fun startRos(domainId: Int, networkInterface: String) {
        // Acquire multicast lock for DDS discovery
        acquireMulticastLock()

        NativeBridge.nativeStartRos(domainId, networkInterface)
        _rosDomainId.value = domainId
        _selectedNetworkInterface.value = networkInterface
        _rosStarted.value = true
        refreshSensorsAndCameras()
    }

    fun onLocationSettingsEnabled() {
        android.util.Log.i("RosViewModel", "User enabled location settings, restarting GPS")
        // GPS manager will handle starting location updates
        val launcher = permissionHandler.getLocationSettingsLauncher()
        gpsManager.startWithChecks(launcher)
        // Clear pending GPS sensor ID since settings are now enabled
        pendingGpsSensorId = null
    }

    fun onLocationSettingsCancelled() {
        android.util.Log.w("RosViewModel", "User cancelled location settings dialog")
        addNotification("GPS: Location services required", Severity.WARNING)

        // Disable the GPS sensor since user declined to enable location settings
        pendingGpsSensorId?.let { sensorId ->
            android.util.Log.i("RosViewModel", "Disabling GPS sensor due to cancelled settings: $sensorId")
            NativeBridge.nativeDisableSensor(sensorId)
            refreshSensors()
            pendingGpsSensorId = null
        }
    }

    fun stopRos() {
        NativeBridge.nativeStopRos()
        releaseMulticastLock()
        _rosStarted.value = false
        refreshSensorsAndCameras()
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

    override fun onCleared() {
        super.onCleared()
        releaseMulticastLock()
    }

    // -- Dashboard navigation --

    fun navigateToRosSetup() {
        _screen.value = Screen.RosSetup
    }

    fun navigateToBuiltInSensors() {
        refreshSensorsAndCameras()
        _screen.value = Screen.BuiltInSensors
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
            is Screen.BuiltInSensors, is Screen.Subsystem, is Screen.RosSetup -> {
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
        // For GPS sensor, the native callback will trigger GPS start via GpsManager
        // For other sensors, just enable directly
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
        // Note: Don't clear pendingGpsSensorId here - we still need to handle
        // the location settings dialog that may follow
    }

    fun onLocationPermissionDenied() {
        android.util.Log.w("RosViewModel", "Location permission denied by user")
        addNotification("GPS: Location permission required", Severity.WARNING)

        // Disable the GPS sensor since user denied location permission
        pendingGpsSensorId?.let { sensorId ->
            android.util.Log.i("RosViewModel", "Disabling GPS sensor due to denied permission: $sensorId")
            NativeBridge.nativeDisableSensor(sensorId)
            refreshSensors()
            pendingGpsSensorId = null
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

    private fun refreshCameras() {
        try {
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
                name = "ZED Stereo Node",
                description = "Captures stereo image data from the ZED camera. Runs on an external NVIDIA Jetson/PC and streams to Android via DDS.",
                state = NodeState.Stopped,
                subscribesTo = emptyList(),
                publishesTo = listOf(
                    TopicInfo("/stereo_image_data", "sensor_msgs/msg/Image")
                ),
                upstreamNodeId = null,
                isExternal = true
            ),
            PipelineNode(
                id = "yolo_obj_detect",
                name = "YOLO Object Detection",
                description = "Subscribes to stereo image data and runs YOLO object detection to identify objects in 3D space.",
                subscribesTo = listOf(
                    TopicInfo("/stereo_image_data", "sensor_msgs/msg/Image")
                ),
                publishesTo = listOf(
                    TopicInfo("/object_xyz_pos", "geometry_msgs/msg/PointStamped")
                ),
                upstreamNodeId = "zed_stereo_node"
            ),
            PipelineNode(
                id = "laser_positioning",
                name = "Laser Positioning",
                description = "Consumes detected object coordinates and determines the required positioning and targeting for actuation.",
                subscribesTo = listOf(
                    TopicInfo("/object_xyz_pos", "geometry_msgs/msg/PointStamped")
                ),
                publishesTo = listOf(
                    TopicInfo("/stepper_steps", "std_msgs/msg/Int32MultiArray")
                ),
                upstreamNodeId = "yolo_obj_detect"
            ),
            PipelineNode(
                id = "micro_ros_agent",
                name = "micro-ROS Agent",
                description = "Mediates communication between the ROS 2 network and the microcontroller via Serial/DDS bridge.",
                subscribesTo = listOf(
                    TopicInfo("/stepper_steps", "std_msgs/msg/Int32MultiArray")
                ),
                publishesTo = emptyList(),
                upstreamNodeId = "laser_positioning"
            )
        )
    }
}
