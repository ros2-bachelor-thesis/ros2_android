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
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

/**
 * Manages the ROS 2 perception & positioning pipeline state machine.
 * Handles node lifecycle, topic probing, and distributed execution tracking.
 */
class PipelineStateMachine(
    private val applicationContext: Context,
    private val coroutineScope: CoroutineScope,
    private val onError: (String) -> Unit,
    private val onPerceptionStateChange: (Boolean) -> Unit
) {

    private val _pipelineNodes = MutableStateFlow(createDefaultPipelineNodes())
    val pipelineNodes: StateFlow<List<PipelineNode>> = _pipelineNodes

    private val _pipelineState = MutableStateFlow(PipelineState.STOPPED)
    val pipelineState: StateFlow<PipelineState> = _pipelineState

    private val _isProbing = MutableStateFlow(false)
    val isProbing: StateFlow<Boolean> = _isProbing

    private val _discoveredTopics = MutableStateFlow<Set<String>>(emptySet())
    val discoveredTopics: StateFlow<Set<String>> = _discoveredTopics

    private val nodeRuntimeStates = mutableMapOf<String, NodeRuntimeState>()

    // -- Query methods --

    fun canStartNodeLocally(nodeId: String): Boolean {
        val runtime = nodeRuntimeStates[nodeId]
        if (runtime?.detectedOnNetwork == true) return false // Already running elsewhere

        return when (nodeId) {
            "object_detection" -> _pipelineState.value == PipelineState.ZED_AVAILABLE
            "target_manager" -> _pipelineState.value == PipelineState.DETECTION_RUNNING
            "arm_commander" -> _pipelineState.value == PipelineState.TARGET_RUNNING
            "micro_ros_agent" -> _pipelineState.value == PipelineState.ARM_RUNNING
            else -> false
        }
    }

    fun isNodeRunningLocally(nodeId: String): Boolean {
        return nodeRuntimeStates[nodeId]?.runningLocally == true
    }

    fun isNodeDetectedOnNetwork(nodeId: String): Boolean {
        return nodeRuntimeStates[nodeId]?.detectedOnNetwork == true
    }

    fun getNodeDisplayState(nodeId: String): NodeState {
        val runtime = nodeRuntimeStates[nodeId]
        return if (runtime?.isRunning == true) NodeState.Running else NodeState.Stopped
    }

    fun isNodeStartable(nodeId: String): Boolean {
        return canStartNodeLocally(nodeId)
    }

    // -- Node lifecycle --

    fun startNode(nodeId: String) {
        coroutineScope.launch(Dispatchers.IO) {
            try {
                when (nodeId) {
                    "object_detection" -> {
                        val modelsPath = "${applicationContext.filesDir.absolutePath}/models"
                        NativeBridge.enablePerception(modelsPath)
                        nodeRuntimeStates["object_detection"] = NodeRuntimeState(runningLocally = true)
                        withContext(Dispatchers.Main) {
                            _pipelineState.value = PipelineState.DETECTION_RUNNING
                            onPerceptionStateChange(true)
                        }
                    }
                    "target_manager" -> {
                        // TODO: Implement target manager start
                        nodeRuntimeStates["target_manager"] = NodeRuntimeState(runningLocally = true)
                        withContext(Dispatchers.Main) {
                            _pipelineState.value = PipelineState.TARGET_RUNNING
                        }
                    }
                    "arm_commander" -> {
                        // TODO: Implement arm commander start
                        nodeRuntimeStates["arm_commander"] = NodeRuntimeState(runningLocally = true)
                        withContext(Dispatchers.Main) {
                            _pipelineState.value = PipelineState.ARM_RUNNING
                        }
                    }
                    "micro_ros_agent" -> {
                        // TODO: Implement micro-ROS agent start
                        nodeRuntimeStates["micro_ros_agent"] = NodeRuntimeState(runningLocally = true)
                        withContext(Dispatchers.Main) {
                            _pipelineState.value = PipelineState.AGENT_RUNNING
                        }
                    }
                }
            } catch (e: Exception) {
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
                // Stop the node itself
                when (nodeId) {
                    "object_detection" -> {
                        if (nodeRuntimeStates["object_detection"]?.runningLocally == true) {
                            NativeBridge.disablePerception()
                        }
                        nodeRuntimeStates.remove("object_detection")
                        withContext(Dispatchers.Main) {
                            onPerceptionStateChange(false)
                        }
                    }
                    "target_manager" -> {
                        // TODO: Implement target manager stop
                        nodeRuntimeStates.remove("target_manager")
                    }
                    "arm_commander" -> {
                        // TODO: Implement arm commander stop
                        nodeRuntimeStates.remove("arm_commander")
                    }
                    "micro_ros_agent" -> {
                        // TODO: Implement micro-ROS agent stop
                        nodeRuntimeStates.remove("micro_ros_agent")
                    }
                }

                // Stop all downstream nodes
                stopDownstreamNodes(nodeId)

                // Rollback pipeline state
                val newState = when (nodeId) {
                    "object_detection" -> PipelineState.ZED_AVAILABLE
                    "target_manager" -> PipelineState.DETECTION_RUNNING
                    "arm_commander" -> PipelineState.TARGET_RUNNING
                    "micro_ros_agent" -> PipelineState.ARM_RUNNING
                    else -> _pipelineState.value
                }

                withContext(Dispatchers.Main) {
                    _pipelineState.value = newState
                }
            } catch (e: Exception) {
                android.util.Log.e("PipelineStateMachine", "Failed to stop node $nodeId", e)
                withContext(Dispatchers.Main) {
                    onError("Failed to stop $nodeId: ${e.message}")
                }
            }
        }
    }

    fun toggleNodeState(nodeId: String) {
        val runtime = nodeRuntimeStates[nodeId]

        if (runtime?.runningLocally == true) {
            stopNode(nodeId)
        } else if (runtime?.detectedOnNetwork != true) {
            if (canStartNodeLocally(nodeId)) {
                startNode(nodeId)
            }
        }
    }

    // -- Topic probing --

    fun toggleTopicProbing() {
        _isProbing.value = !_isProbing.value
        if (_isProbing.value) {
            // Start probing
            _pipelineState.value = PipelineState.ZED_PROBING
            coroutineScope.launch {
                while (_isProbing.value) {
                    try {
                        val topics = NativeBridge.nativeGetDiscoveredTopics()
                        _discoveredTopics.value = topics.toSet()

                        // Update state machine based on discovered topics
                        advancePipelineState(topics.toSet())
                    } catch (_: UnsatisfiedLinkError) {
                        _isProbing.value = false
                        _pipelineState.value = PipelineState.STOPPED
                        return@launch
                    } catch (e: Exception) {
                        android.util.Log.e("PipelineStateMachine", "Failed to probe topics", e)
                    }
                    delay(2000)
                }
            }
        } else {
            // Stop probing - reset pipeline state based on what's actually running locally
            val newState = when {
                nodeRuntimeStates["micro_ros_agent"]?.runningLocally == true -> PipelineState.AGENT_RUNNING
                nodeRuntimeStates["arm_commander"]?.runningLocally == true -> PipelineState.ARM_RUNNING
                nodeRuntimeStates["target_manager"]?.runningLocally == true -> PipelineState.TARGET_RUNNING
                nodeRuntimeStates["object_detection"]?.runningLocally == true -> PipelineState.DETECTION_RUNNING
                else -> PipelineState.STOPPED
            }
            _pipelineState.value = newState

            // Clear network detection states when probing stops
            nodeRuntimeStates.replaceAll { _, state ->
                if (state.detectedOnNetwork) {
                    NodeRuntimeState(runningLocally = state.runningLocally, detectedOnNetwork = false)
                } else {
                    state
                }
            }
        }
    }

    // -- Private helpers --

    private fun stopDownstreamNodes(nodeId: String) {
        val downstreamOrder = when (nodeId) {
            "object_detection" -> listOf("target_manager", "arm_commander", "micro_ros_agent")
            "target_manager" -> listOf("arm_commander", "micro_ros_agent")
            "arm_commander" -> listOf("micro_ros_agent")
            else -> emptyList()
        }

        for (downstream in downstreamOrder) {
            if (nodeRuntimeStates[downstream]?.runningLocally == true) {
                when (downstream) {
                    "object_detection" -> NativeBridge.disablePerception()
                    // TODO: Add other node stop calls
                }
                nodeRuntimeStates.remove(downstream)
            }
        }
    }

    private fun advancePipelineState(discoveredTopics: Set<String>) {
        val nodes = _pipelineNodes.value

        when (_pipelineState.value) {
            PipelineState.STOPPED -> {
                // No action - wait for user to start probing
            }
            PipelineState.ZED_PROBING -> {
                // Check if object detection's required topics are available
                val detectionNode = nodes.find { it.id == "object_detection" }
                if (detectionNode != null && checkNodeAvailability(detectionNode.subscribesTo, discoveredTopics)) {
                    // Mark ZED node as running on network
                    nodeRuntimeStates["zed_stereo_node"] = NodeRuntimeState(detectedOnNetwork = true)
                    _pipelineState.value = PipelineState.ZED_AVAILABLE
                }
            }
            PipelineState.ZED_AVAILABLE -> {
                // Check if ZED topics disappeared
                val detectionNode = nodes.find { it.id == "object_detection" }
                if (detectionNode != null && !checkNodeAvailability(detectionNode.subscribesTo, discoveredTopics)) {
                    // ZED went offline - go back to probing
                    nodeRuntimeStates.remove("zed_stereo_node")
                    _pipelineState.value = PipelineState.ZED_PROBING
                    return
                }

                // Check if object detection is running elsewhere
                if (detectionNode != null && checkNodeAvailability(detectionNode.publishesTo, discoveredTopics)) {
                    nodeRuntimeStates["object_detection"] = NodeRuntimeState(detectedOnNetwork = true)
                    _pipelineState.value = PipelineState.DETECTION_RUNNING
                }
            }
            PipelineState.DETECTION_RUNNING -> {
                // Check if detection topics disappeared
                val detectionNode = nodes.find { it.id == "object_detection" }
                if (detectionNode != null && !checkNodeAvailability(detectionNode.publishesTo, discoveredTopics)) {
                    // Detection went offline - go back to ZED_AVAILABLE
                    nodeRuntimeStates.remove("object_detection")
                    _pipelineState.value = PipelineState.ZED_AVAILABLE
                    return
                }

                // Check if target manager is running elsewhere
                val targetNode = nodes.find { it.id == "target_manager" }
                if (targetNode != null && checkNodeAvailability(targetNode.publishesTo, discoveredTopics)) {
                    nodeRuntimeStates["target_manager"] = NodeRuntimeState(detectedOnNetwork = true)
                    _pipelineState.value = PipelineState.TARGET_RUNNING
                }
            }
            PipelineState.TARGET_RUNNING -> {
                // Check if target topics disappeared
                val targetNode = nodes.find { it.id == "target_manager" }
                if (targetNode != null && !checkNodeAvailability(targetNode.publishesTo, discoveredTopics)) {
                    // Target went offline - go back to DETECTION_RUNNING
                    nodeRuntimeStates.remove("target_manager")
                    _pipelineState.value = PipelineState.DETECTION_RUNNING
                    return
                }

                // Check if arm commander is running elsewhere
                val armNode = nodes.find { it.id == "arm_commander" }
                if (armNode != null && checkNodeAvailability(armNode.publishesTo, discoveredTopics)) {
                    nodeRuntimeStates["arm_commander"] = NodeRuntimeState(detectedOnNetwork = true)
                    _pipelineState.value = PipelineState.ARM_RUNNING
                }
            }
            PipelineState.ARM_RUNNING -> {
                // Check if arm topics disappeared
                val armNode = nodes.find { it.id == "arm_commander" }
                if (armNode != null && !checkNodeAvailability(armNode.publishesTo, discoveredTopics)) {
                    // Arm went offline - go back to TARGET_RUNNING
                    nodeRuntimeStates.remove("arm_commander")
                    _pipelineState.value = PipelineState.TARGET_RUNNING
                }
                // Micro-ROS agent doesn't publish topics, so can't detect
            }
            PipelineState.AGENT_RUNNING -> {
                // Already at final state (micro-ROS agent has no topics to monitor)
            }
        }
    }

    private fun checkNodeAvailability(
        publishedTopics: List<TopicInfo>,
        discoveredTopics: Set<String>
    ): Boolean {
        return publishedTopics.all { it.name in discoveredTopics }
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
                description = "Selects primary targets (CPB eggs) and performs IMU-based orientation calibration for laser positioning. Computes pan/tilt commands with offset correction.",
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
