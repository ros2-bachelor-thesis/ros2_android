package com.github.mowerick.ros2.android.viewmodel

import android.content.Context
import android.graphics.Bitmap
import android.net.wifi.WifiManager
import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.github.mowerick.ros2.android.GpsManager
import com.github.mowerick.ros2.android.MainActivity
import com.github.mowerick.ros2.android.NativeBridge
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
import org.json.JSONArray
import org.json.JSONObject

sealed class Screen {
    data object Dashboard : Screen()
    data object RosSetup : Screen()
    data object BuiltInSensors : Screen()
    data object Subsystem : Screen()
    data class SensorDetail(val sensorId: String) : Screen()
    data class CameraDetail(val cameraId: String) : Screen()
    data class NodeDetail(val nodeId: String) : Screen()
}

class RosViewModel(private val applicationContext: Context) : ViewModel() {

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

    private var polling = false
    private var cameraPreviewPolling = false
    private val _isProbing = MutableStateFlow(false)
    val isProbing: StateFlow<Boolean> = _isProbing

    init {
        // Register notification callback instead of polling
        NativeBridge.setNotificationCallback { severity, message ->
            viewModelScope.launch {
                addNotification(message, if (severity == "ERROR") Severity.ERROR else Severity.WARNING)
            }
        }

        // Register GPS enable/disable callbacks
        NativeBridge.setGpsCallbacks(
            onEnable = {
                android.util.Log.i("RosViewModel", "GPS enable callback received")
                viewModelScope.launch {
                    android.util.Log.i("RosViewModel", "Calling tryStartGps()")
                    tryStartGps()
                }
            },
            onDisable = {
                android.util.Log.i("RosViewModel", "GPS disable callback received")
                gpsManager.stop()
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
            val json = NativeBridge.nativeGetNetworkInterfaces()
            val arr = JSONArray(json)
            val ifaces = mutableListOf<String>()
            for (i in 0 until arr.length()) {
                ifaces.add(arr.getString(i))
            }
            _networkInterfaces.value = ifaces
        } catch (e: Exception) {
            android.util.Log.e("RosViewModel", "Failed to load network interfaces", e)
        }
    }

    fun refreshNetworkInterfaces() {
        try {
            val ifaces = MainActivity.queryNetworkInterfaces()
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

    private fun tryStartGps() {
        android.util.Log.i("RosViewModel", "tryStartGps() called")

        // Check if we have permission
        if (!MainActivity.hasLocationPermission()) {
            android.util.Log.e("RosViewModel", "GPS: No location permission")
            addNotification("GPS: Location permission required", Severity.WARNING)
            return
        }

        android.util.Log.i("RosViewModel", "GPS: Permission granted")

        // Check location settings and prompt user if needed
        val launcher = MainActivity.getLocationSettingsLauncher()
        if (launcher != null) {
            android.util.Log.i("RosViewModel", "GPS: Checking location settings")
            gpsManager.checkLocationSettings(
                launcher,
                onSuccess = {
                    android.util.Log.i("RosViewModel", "GPS: Location settings OK, starting GPS")
                    // Settings are OK, start GPS
                    startGpsLocationUpdates()
                },
                onFailure = {
                    android.util.Log.w("RosViewModel", "GPS: Location settings check failed")
                    // Error occurred
                    addNotification("GPS: Location settings check failed", Severity.ERROR)
                }
            )
        } else {
            android.util.Log.w("RosViewModel", "GPS: No launcher available, trying direct start")
            // Fallback to direct start if no launcher
            startGpsLocationUpdates()
        }
    }

    private fun startGpsLocationUpdates() {
        val started = gpsManager.start()
        if (!started) {
            val status = gpsManager.getStatus()
            android.util.Log.e("RosViewModel", "GPS: Failed to start, status=$status")
            val message = when {
                status.contains("Permission") -> "GPS: Location permission required"
                status.contains("disabled") -> "GPS: Location services are disabled"
                else -> "GPS: Failed to start"
            }
            addNotification(message, Severity.ERROR)
        } else {
            android.util.Log.i("RosViewModel", "GPS: Started successfully")
        }
    }

    fun onLocationSettingsEnabled() {
        android.util.Log.i("RosViewModel", "User enabled location settings")

        // If this was triggered from enableSensor, we need to complete the enable
        // Check if GPS sensor is not yet enabled
        val gpsSensor = _sensors.value.find { it.uniqueId == "gps_location_provider" }
        if (gpsSensor != null && !gpsSensor.enabled) {
            android.util.Log.i("RosViewModel", "Completing GPS sensor enable after location settings enabled")
            NativeBridge.nativeEnableSensor("gps_location_provider")
            refreshSensors()
        } else {
            // GPS was already enabled, just start updates
            android.util.Log.i("RosViewModel", "GPS already enabled, starting location updates")
            startGpsLocationUpdates()
        }
    }

    fun onLocationSettingsCancelled() {
        android.util.Log.w("RosViewModel", "User cancelled location settings dialog")
        addNotification("GPS: Location services required", Severity.WARNING)
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
        startPolling(sensor.uniqueId)
    }

    fun navigateToCamera(camera: CameraInfo) {
        _screen.value = Screen.CameraDetail(camera.uniqueId)
        if (camera.enabled) {
            startCameraPreview(camera.uniqueId)
        }
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
        stopPolling()
        stopCameraPreview()
        when (_screen.value) {
            is Screen.SensorDetail, is Screen.CameraDetail -> {
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
        startCameraPreview(uniqueId)
    }

    fun disableCamera(uniqueId: String) {
        stopCameraPreview()
        NativeBridge.nativeDisableCamera(uniqueId)
        refreshCameras()
    }

    fun enableSensor(uniqueId: String) {
        android.util.Log.i("RosViewModel", "enableSensor called for: $uniqueId")

        // Special handling for GPS sensor - check location settings first
        if (uniqueId == "gps_location_provider") {
            android.util.Log.i("RosViewModel", "GPS sensor enable requested, checking prerequisites")

            // Check permission first
            if (!MainActivity.hasLocationPermission()) {
                android.util.Log.e("RosViewModel", "GPS: No location permission")
                addNotification("GPS: Location permission required", Severity.WARNING)
                return
            }

            android.util.Log.i("RosViewModel", "GPS: Permission granted, checking location settings")

            // Check and prompt for location settings before enabling
            val launcher = MainActivity.getLocationSettingsLauncher()
            if (launcher != null) {
                gpsManager.checkLocationSettings(
                    launcher,
                    onSuccess = {
                        android.util.Log.i("RosViewModel", "GPS: Location settings OK, enabling sensor")
                        // Settings OK, proceed with enable
                        NativeBridge.nativeEnableSensor(uniqueId)
                        refreshSensors()
                    },
                    onFailure = {
                        android.util.Log.e("RosViewModel", "GPS: Location settings check failed")
                        addNotification("GPS: Location settings check failed", Severity.ERROR)
                    }
                )
            } else {
                android.util.Log.w("RosViewModel", "GPS: No launcher, proceeding anyway")
                // No launcher available, proceed anyway (GPS manager will handle it)
                NativeBridge.nativeEnableSensor(uniqueId)
                refreshSensors()
            }
        } else {
            // Non-GPS sensors: enable directly
            NativeBridge.nativeEnableSensor(uniqueId)
            refreshSensors()
        }
    }

    fun disableSensor(uniqueId: String) {
        NativeBridge.nativeDisableSensor(uniqueId)
        refreshSensors()
    }

    fun onLocationPermissionGranted() {
        if (_rosStarted.value && !gpsManager.isRunning()) {
            tryStartGps()
        }
    }

    // -- Private helpers --

    private fun refreshSensorsAndCameras() {
        refreshSensors()
        refreshCameras()
    }

    private fun refreshSensors() {
        try {
            val json = NativeBridge.nativeGetSensorList()
            val arr = JSONArray(json)
            val list = mutableListOf<SensorInfo>()
            for (i in 0 until arr.length()) {
                val obj = arr.getJSONObject(i)
                list.add(
                    SensorInfo(
                        uniqueId = obj.getString("uniqueId"),
                        prettyName = obj.getString("prettyName"),
                        sensorName = obj.getString("sensorName"),
                        vendor = obj.getString("vendor"),
                        topicName = obj.getString("topicName"),
                        topicType = obj.getString("topicType"),
                        enabled = obj.optBoolean("enabled", false)
                    )
                )
            }
            _sensors.value = list
        } catch (e: Exception) {
            android.util.Log.e("RosViewModel", "Failed to refresh sensors", e)
        }
    }

    private fun refreshCameras() {
        try {
            val json = NativeBridge.nativeGetCameraList()
            val arr = JSONArray(json)
            val list = mutableListOf<CameraInfo>()
            for (i in 0 until arr.length()) {
                val obj = arr.getJSONObject(i)
                list.add(
                    CameraInfo(
                        uniqueId = obj.getString("uniqueId"),
                        name = obj.getString("name"),
                        enabled = obj.getBoolean("enabled"),
                        imageTopicName = obj.getString("imageTopicName"),
                        imageTopicType = obj.getString("imageTopicType"),
                        infoTopicName = obj.getString("infoTopicName"),
                        infoTopicType = obj.getString("infoTopicType"),
                        resolutionWidth = obj.getInt("resolutionWidth"),
                        resolutionHeight = obj.getInt("resolutionHeight"),
                        isFrontFacing = obj.optBoolean("isFrontFacing", false),
                        sensorOrientation = obj.optInt("sensorOrientation", 0)
                    )
                )
            }
            _cameras.value = list
        } catch (e: Exception) {
            android.util.Log.e("RosViewModel", "Failed to refresh cameras", e)
        }
    }

    private fun startPolling(uniqueId: String) {
        polling = true
        viewModelScope.launch {
            while (polling) {
                try {
                    val json = NativeBridge.nativeGetSensorData(uniqueId)
                    val obj = JSONObject(json)
                    if (obj.has("values")) {
                        val valuesArr = obj.getJSONArray("values")
                        val values = mutableListOf<Double>()
                        for (i in 0 until valuesArr.length()) {
                            values.add(valuesArr.getDouble(i))
                        }
                        _currentReading.value = SensorReading(
                            values = values,
                            unit = obj.getString("unit")
                        )
                    }
                } catch (e: Exception) {
                    android.util.Log.e("RosViewModel", "Failed to poll sensor data for $uniqueId", e)
                }
                delay(100)
            }
        }
    }

    private fun stopPolling() {
        polling = false
        _currentReading.value = null
    }

    private fun startCameraPreview(uniqueId: String) {
        cameraPreviewPolling = true
        viewModelScope.launch {
            while (cameraPreviewPolling) {
                try {
                    val bytes = NativeBridge.nativeGetCameraFrame(uniqueId)
                    if (bytes != null && bytes.size > 8) {
                        val width = ((bytes[0].toInt() and 0xFF) shl 24) or
                            ((bytes[1].toInt() and 0xFF) shl 16) or
                            ((bytes[2].toInt() and 0xFF) shl 8) or
                            (bytes[3].toInt() and 0xFF)
                        val height = ((bytes[4].toInt() and 0xFF) shl 24) or
                            ((bytes[5].toInt() and 0xFF) shl 16) or
                            ((bytes[6].toInt() and 0xFF) shl 8) or
                            (bytes[7].toInt() and 0xFF)
                        val pixelCount = width * height
                        val expectedSize = 8 + pixelCount * 3
                        if (bytes.size >= expectedSize && width > 0 && height > 0) {
                            val pixels = IntArray(pixelCount)
                            for (i in 0 until pixelCount) {
                                val offset = 8 + i * 3
                                val r = bytes[offset].toInt() and 0xFF
                                val g = bytes[offset + 1].toInt() and 0xFF
                                val b = bytes[offset + 2].toInt() and 0xFF
                                pixels[i] = (0xFF shl 24) or (r shl 16) or (g shl 8) or b
                            }
                            val bitmap = Bitmap.createBitmap(width, height, Bitmap.Config.ARGB_8888)
                            bitmap.setPixels(pixels, 0, width, 0, 0, width, height)
                            _cameraFrame.value = bitmap
                        }
                    }
                } catch (e: Exception) {
                    android.util.Log.e("RosViewModel", "Failed to get camera frame for $uniqueId", e)
                }
                delay(100)
            }
        }
    }

    private fun stopCameraPreview() {
        cameraPreviewPolling = false
        _cameraFrame.value = null
    }

    fun toggleTopicProbing() {
        _isProbing.value = !_isProbing.value
        if (_isProbing.value) {
            viewModelScope.launch {
                while (_isProbing.value) {
                    try {
                        val json = NativeBridge.nativeGetDiscoveredTopics()
                        val arr = JSONArray(json)
                        val topics = mutableSetOf<String>()
                        for (i in 0 until arr.length()) {
                            topics.add(arr.getString(i))
                        }
                        _discoveredTopics.value = topics
                        updateExternalNodeStates(topics)
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
