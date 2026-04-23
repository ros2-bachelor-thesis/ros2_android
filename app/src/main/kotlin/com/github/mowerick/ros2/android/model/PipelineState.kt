package com.github.mowerick.ros2.android.model

/**
 * Represents the current state of the ROS 2 perception & positioning pipeline.
 *
 * The pipeline is a linear state machine with explicit forward/backward transitions.
 * Each state represents which subsystem is currently active and determines which
 * nodes can be started next.
 */
enum class PipelineState {
    /**
     * Initial state - no nodes running.
     * User must trigger topic probing to begin.
     */
    STOPPED,

    /**
     * Actively probing for ZED camera topics on the network.
     */
    ZED_PROBING,

    /**
     * ZED camera topics discovered on network.
     * Object detection can now be started (locally or detected externally).
     */
    ZED_AVAILABLE,

    /**
     * Object detection is running (locally or on another device).
     * Publishes: /cpb_beetle_center, /cpb_larva_center, /cpb_eggs_center, etc.
     * Target manager can now be started.
     */
    DETECTION_RUNNING,

    /**
     * Target manager is running (locally or on another device).
     * Publishes: ESP32_Command
     * micro-ROS agent can now be started.
     */
    TARGET_RUNNING,

    /**
     * micro-ROS agent running, bridging ESP32_Command/ESP32_Feedback
     * between ROS 2 DDS network and ESP32 microcontroller via USB serial.
     */
    COMMAND_ACTIVE;

    fun getDescription(): String = when (this) {
        STOPPED -> "Pipeline inactive"
        ZED_PROBING -> "Searching for ZED camera..."
        ZED_AVAILABLE -> "ZED camera available"
        DETECTION_RUNNING -> "Object detection active"
        TARGET_RUNNING -> "Target manager active"
        COMMAND_ACTIVE -> "Full pipeline active"
    }

    companion object {
        val stateTransitions: Map<PipelineState, Pair<PipelineState?, PipelineState?>> = mapOf(
            STOPPED            to Pair(null,                ZED_PROBING),
            ZED_PROBING        to Pair(STOPPED,             ZED_AVAILABLE),
            ZED_AVAILABLE      to Pair(ZED_PROBING,         DETECTION_RUNNING),
            DETECTION_RUNNING  to Pair(ZED_AVAILABLE,       TARGET_RUNNING),
            TARGET_RUNNING     to Pair(DETECTION_RUNNING,   COMMAND_ACTIVE),
            COMMAND_ACTIVE     to Pair(TARGET_RUNNING,      null),
        )

        fun nextState(current: PipelineState): PipelineState? = stateTransitions[current]?.second
        fun previousState(current: PipelineState): PipelineState? = stateTransitions[current]?.first
    }
}
