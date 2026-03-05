package com.github.sloretz.sensors_for_ros.model

data class CameraInfo(
    val uniqueId: String,
    val name: String,
    val enabled: Boolean,
    val imageTopicName: String,
    val imageTopicType: String,
    val infoTopicName: String,
    val infoTopicType: String,
    val resolutionWidth: Int,
    val resolutionHeight: Int
)
