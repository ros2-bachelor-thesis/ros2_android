package com.github.sloretz.sensors_for_ros.model

data class SensorInfo(
    val uniqueId: String,
    val prettyName: String,
    val sensorName: String,
    val vendor: String,
    val topicName: String,
    val topicType: String
)
