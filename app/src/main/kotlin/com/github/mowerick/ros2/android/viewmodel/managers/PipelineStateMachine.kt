package com.github.mowerick.ros2.android.viewmodel.managers

import android.content.Context
import com.github.mowerick.ros2.android.util.NativeBridge
import com.github.mowerick.ros2.android.model.NodeRuntimeState
import com.github.mowerick.ros2.android.model.NodeState
import com.github.mowerick.ros2.android.model.PipelineNode
import com.github.mowerick.ros2.android.model.PipelineState
import com.github.mowerick.ros2.android.model.TopicInfo
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

/**
 * Manages the ROS 2 perception & positioning pipeline state machine.
 * Handles node lifecycle, per-node topic probing, and distributed execution tracking.
 *
 * Each pipeline node can independently probe for its topics on the network,
 * enabling distributed deployments where different nodes run on different devices.
 */
class PipelineStateMachine(
    private val applicationContext: Context,
    private val coroutineScope: CoroutineScope,
    private val onError: (String) -> Unit,
    private val onPerceptionStateChange: (Boolean) -> Unit
) {

    private val _pipelineNodes = MutableStateFlow(createDefaultPipelineNodes())
    val pipelineNodes: StateFlow<List<PipelineNode>> = _pipelineNodes

    private val _nodeStates = MutableStateFlow<Map<String, NodeRuntimeState>>(emptyMap())
    val nodeStates: StateFlow<Map<String, NodeRuntimeState>> = _nodeStates

    private val _pipelineState = MutableStateFlow(PipelineState.STOPPED)
    val pipelineState: StateFlow<PipelineState> = _pipelineState

    // Device ID of connected ESP32 for micro-ROS Agent (set by ExternalDeviceManager)
    var microRosDeviceId: String? = null

    private val _discoveredTopics = MutableStateFlow<Set<String>>(emptySet())
    val discoveredTopics: StateFlow<Set<String>> = _discoveredTopics

    private var sharedPollingJob: Job? = null

    // -- Query methods --

    fun canStartNodeLocally(nodeId: String): Boolean {
        val node = _pipelineNodes.value.find { it.id == nodeId } ?: return false
        if (node.isExternal) return false
        val state = _nodeStates.value[nodeId]
        if (state?.runningLocally == true || state?.detectedOnNetwork == true) return false

        // Check if upstream node is active (locally or on network)
        val upstreamId = node.upstreamNodeId ?: return false
        val upstreamState = _nodeStates.value[upstreamId]
        return upstreamState?.runningLocally == true || upstreamState?.detectedOnNetwork == true
    }

    fun isNodeRunningLocally(nodeId: String): Boolean {
        return _nodeStates.value[nodeId]?.runningLocally == true
    }

    fun isNodeDetectedOnNetwork(nodeId: String): Boolean {
        return _nodeStates.value[nodeId]?.detectedOnNetwork == true
    }

    fun isNodeStarting(nodeId: String): Boolean {
        return _nodeStates.value[nodeId]?.isStarting == true
    }

    fun getNodeDisplayState(nodeId: String): NodeState {
        val state = _nodeStates.value[nodeId]
        return if (state?.isRunning == true) NodeState.Running else NodeState.Stopped
    }

    fun isNodeStartable(nodeId: String): Boolean {
        return canStartNodeLocally(nodeId)
    }

    fun canProbeNode(nodeId: String): Boolean {
        val state = _nodeStates.value[nodeId]
        // Allow stopping an active probe
        if (state?.isProbing == true) return true
        // Can't probe if already running locally
        if (state?.runningLocally == true) return false

        // Check if the pipeline FSM is in the correct state for this node to probe
        return when (nodeId) {
            "zed_stereo_node" -> _pipelineState.value in listOf(
                PipelineState.STOPPED, PipelineState.ZED_PROBING, PipelineState.ZED_AVAILABLE
            )
            "object_detection" -> _pipelineState.value >= PipelineState.ZED_AVAILABLE
            "target_manager" -> _pipelineState.value >= PipelineState.DETECTION_RUNNING
            "micro_ros_agent" -> _pipelineState.value >= PipelineState.TARGET_RUNNING
            else -> false
        }
    }

    // -- Node lifecycle --

    fun startNode(nodeId: String) {
        updateNodeState(nodeId) { it.copy(isStarting = true) }
        coroutineScope.launch(Dispatchers.IO) {
            try {
                when (nodeId) {
                    "object_detection" -> {
                        val modelsPath = "${applicationContext.filesDir.absolutePath}/models"
                        NativeBridge.enablePerception(modelsPath)
                        withContext(Dispatchers.Main) {
                            onPerceptionStateChange(true)
                        }
                    }
                    "target_manager" -> {
                        NativeBridge.enableTargetManager()
                    }
                    "micro_ros_agent" -> {
                        val deviceId = microRosDeviceId
                        if (deviceId == null) {
                            throw IllegalStateException("No ESP32 device connected for micro-ROS Agent")
                        }
                        NativeBridge.enableMicroRosAgent(deviceId, 460800)
                    }
                }
                updateNodeState(nodeId) { it.copy(runningLocally = true, isProbing = false, isStarting = false) }

                // Only advance FSM if not already at AGENT_RUNNING
                if (_pipelineState.value != PipelineState.AGENT_RUNNING) {
                    advanceState()
                }
                updatePolling()
            } catch (e: Exception) {
                updateNodeState(nodeId) { it.copy(isStarting = false) }
                android.util.Log.e("PipelineStateMachine", "Failed to start node $nodeId", e)
                withContext(Dispatchers.Main) {
                    onError("Failed to start $nodeId: ${e.message}")
                }
            }
        }
    }

    fun stopNode(nodeId: String) {
        coroutineScope.launch(Dispatchers.IO) {
            try {
                when (nodeId) {
                    "object_detection" -> {
                        if (_nodeStates.value["object_detection"]?.runningLocally == true) {
                            NativeBridge.disablePerception()
                        }
                        withContext(Dispatchers.Main) {
                            onPerceptionStateChange(false)
                        }
                    }
                    "target_manager" -> {
                        if (_nodeStates.value["target_manager"]?.runningLocally == true) {
                            NativeBridge.disableTargetManager()
                        }
                    }
                    "micro_ros_agent" -> {
                        if (_nodeStates.value["micro_ros_agent"]?.runningLocally == true) {
                            NativeBridge.disableMicroRosAgent()
                        }
                    }
                }

                stopDownstreamNodes(nodeId)
                removeNodeState(nodeId)
                rollbackState()
                updatePolling()
            } catch (e: Exception) {
                android.util.Log.e("PipelineStateMachine", "Failed to stop node $nodeId", e)
                withContext(Dispatchers.Main) {
                    onError("Failed to stop $nodeId: ${e.message}")
                }
            }
        }
    }

    fun toggleNodeState(nodeId: String) {
        val state = _nodeStates.value[nodeId]

        if (state?.runningLocally == true) {
            stopNode(nodeId)
        } else if (state?.detectedOnNetwork != true) {
            if (canStartNodeLocally(nodeId)) {
                startNode(nodeId)
            }
        }
    }

    // -- Pipeline reset --

    fun resetPipeline() {
        // Stop any locally running nodes
        for ((nodeId, state) in _nodeStates.value) {
            if (state.runningLocally) {
                when (nodeId) {
                    "object_detection" -> NativeBridge.disablePerception()
                    "target_manager" -> NativeBridge.disableTargetManager()
                    "micro_ros_agent" -> NativeBridge.disableMicroRosAgent()
                }
            }
        }
        onPerceptionStateChange(false)
        sharedPollingJob?.cancel()
        sharedPollingJob = null
        _nodeStates.value = emptyMap()
        _pipelineState.value = PipelineState.STOPPED
        _discoveredTopics.value = emptySet()
    }

    // -- Per-node topic probing --

    fun toggleNodeProbing(nodeId: String) {
        val current = _nodeStates.value[nodeId] ?: NodeRuntimeState()
        val newProbing = !current.isProbing

        if (newProbing) {
            updateNodeState(nodeId) { it.copy(isProbing = true) }
            // Advance from STOPPED to ZED_PROBING when first probe starts
            if (_pipelineState.value == PipelineState.STOPPED) {
                advanceState()
            }
        } else {
            // Stop probing - keep detectedOnNetwork intact
            // (they reflect actual topic presence, managed by evaluateAllNodes)
            updateNodeState(nodeId) { it.copy(isProbing = false) }
            // Rollback from ZED_PROBING to STOPPED when no nodes are probing
            if (_pipelineState.value == PipelineState.ZED_PROBING &&
                !_nodeStates.value.values.any { it.isProbing }) {
                rollbackState()
            }
        }

        updatePolling()
    }

    // -- State transitions --

    private fun advanceState() {
        PipelineState.nextState(_pipelineState.value)?.let { _pipelineState.value = it }
    }

    private fun rollbackState() {
        PipelineState.previousState(_pipelineState.value)?.let { _pipelineState.value = it }
    }

    // -- Private helpers --

    private fun updateNodeState(nodeId: String, transform: (NodeRuntimeState) -> NodeRuntimeState) {
        val current = _nodeStates.value[nodeId] ?: NodeRuntimeState()
        _nodeStates.value = _nodeStates.value.toMutableMap().apply {
            put(nodeId, transform(current))
        }
    }

    private fun removeNodeState(nodeId: String) {
        _nodeStates.value = _nodeStates.value.toMutableMap().apply {
            remove(nodeId)
        }
    }

    private fun updatePolling() {
        val anyProbing = _nodeStates.value.values.any { it.isProbing }
        if (anyProbing && sharedPollingJob == null) {
            sharedPollingJob = coroutineScope.launch {
                while (true) {
                    try {
                        val topics = NativeBridge.nativeGetDiscoveredTopics().toSet()
                        _discoveredTopics.value = topics
                        android.util.Log.d("PipelineStateMachine", "discovered topics: ${topics.joinToString(", ")}")
                        evaluateAllNodes(topics)
                    } catch (_: UnsatisfiedLinkError) {
                        // Native library not loaded - stop all probing
                        _nodeStates.value = _nodeStates.value.mapValues {
                            it.value.copy(isProbing = false)
                        }
                        _pipelineState.value = PipelineState.STOPPED
                        sharedPollingJob = null
                        return@launch
                    } catch (e: Exception) {
                        android.util.Log.e("PipelineStateMachine", "Failed to probe topics", e)
                    }
                    delay(5000)
                }
            }
        } else if (!anyProbing && sharedPollingJob != null) {
            sharedPollingJob?.cancel()
            sharedPollingJob = null
        }
    }

    private fun evaluateAllNodes(discoveredTopics: Set<String>) {
        for (node in _pipelineNodes.value) {
            val state = _nodeStates.value[node.id] ?: continue

            // Only evaluate nodes that are actively probing
            if (!state.isProbing) continue

            // Skip nodes with no publishesTo
            if (node.publishesTo.isEmpty()) continue

            // Check if this node's own published topics are on the network
            val allPubTopicsFound = node.publishesTo.all { it.name in discoveredTopics }

            if (allPubTopicsFound && !state.detectedOnNetwork) {
                updateNodeState(node.id) { it.copy(detectedOnNetwork = true, isProbing = false) }
                android.util.Log.d("PipelineStateMachine",
                    "${node.id} detected on network, advancing state")
                advanceState()
            }
        }
    }

    private fun stopDownstreamNodes(nodeId: String) {
        val downstreamOrder = when (nodeId) {
            "object_detection" -> listOf("target_manager", "micro_ros_agent")
            "target_manager" -> listOf("micro_ros_agent")
            else -> emptyList()
        }

        for (downstream in downstreamOrder) {
            val state = _nodeStates.value[downstream] ?: continue
            // Stop native components if running locally
            if (state.runningLocally) {
                when (downstream) {
                    "object_detection" -> NativeBridge.disablePerception()
                    "target_manager" -> NativeBridge.disableTargetManager()
                    "micro_ros_agent" -> NativeBridge.disableMicroRosAgent()
                }
            }
            // Clear all state (running, probing, detected) for downstream nodes
            removeNodeState(downstream)
        }
    }

    companion object {
        private fun createDefaultPipelineNodes(): List<PipelineNode> = listOf(
            PipelineNode(
                id = "zed_stereo_node",
                name = "ZED 2i Camera",
                description = "Captures stereo image, depth, point cloud, and IMU data from ZED 2i camera. Runs on external NVIDIA Jetson/PC and streams to Android via DDS.",
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
                name = "3D Object Detection",
                description = "Runs 3D Object Detection and Deep SORT to detect and track Colorado Potato Beetle life stages (beetle, larva, eggs) in 3D space using ZED camera data.",
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
                description = "Selects primary targets (CPB eggs) and performs IMU-based orientation calibration for laser positioning. Computes pan/tilt commands with offset correction, publishes ESP32_Command.",
                subscribesTo = listOf(
                    TopicInfo("/cpb_eggs_center", "geometry_msgs/msg/Point"),
                    TopicInfo("/zed/zed_node/imu/data", "sensor_msgs/msg/Imu"),
                    TopicInfo("ESP32_Feedback", "vermin_collector_ros_msgs/msg/Feedback"),
                    TopicInfo("/pan_tilt_fixed_position", "std_msgs/msg/Float32MultiArray")
                ),
                publishesTo = listOf(
                    TopicInfo("ESP32_Command", "vermin_collector_ros_msgs/msg/Command")
                ),
                upstreamNodeId = "object_detection",
                isExternal = false
            ),
            PipelineNode(
                id = "micro_ros_agent",
                name = "micro-ROS Agent",
                description = "XRCE-DDS bridge between ROS 2 DDS network and ESP32-S3 microcontroller via USB CDC-ACM serial at 460800 baud. Bridges ESP32_Command/ESP32_Feedback topics for 3-axis stepper control (pitch, yaw, slide) with laser.",
                subscribesTo = listOf(
                    TopicInfo("ESP32_Command", "vermin_collector_ros_msgs/msg/Command")
                ),
                publishesTo = listOf(
                    TopicInfo("ESP32_Feedback", "vermin_collector_ros_msgs/msg/Feedback")
                ),
                upstreamNodeId = "target_manager",
                isExternal = false
            )
        )
    }
}
