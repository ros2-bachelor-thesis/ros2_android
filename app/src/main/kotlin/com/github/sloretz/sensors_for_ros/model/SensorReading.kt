package com.github.sloretz.sensors_for_ros.model

data class SensorReading(
    val values: List<Double>,
    val unit: String
) {
    val formattedValue: String
        get() = if (values.size == 1) {
            "%.2f %s".format(values[0], unit)
        } else {
            values.joinToString(", ") { "%.2f".format(it) } + " " + unit
        }
}
