package com.github.mowerick.ros2.android.model

data class SensorInfo(
    val uniqueId: String,
    val prettyName: String,
    val sensorName: String,
    val vendor: String,
    val topicName: String,
    val topicType: String,
    val enabled: Boolean = false
)
