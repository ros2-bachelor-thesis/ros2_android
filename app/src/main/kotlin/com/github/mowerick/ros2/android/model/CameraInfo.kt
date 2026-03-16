package com.github.mowerick.ros2.android.model

data class CameraInfo(
    val uniqueId: String,
    val name: String,
    val enabled: Boolean,
    val imageTopicName: String,
    val imageTopicType: String,
    val compressedImageTopicName: String,
    val compressedImageTopicType: String,
    val infoTopicName: String,
    val infoTopicType: String,
    val resolutionWidth: Int,
    val resolutionHeight: Int,
    val isFrontFacing: Boolean,
    val sensorOrientation: Int
)
