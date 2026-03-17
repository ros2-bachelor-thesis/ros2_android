package com.github.mowerick.ros2.android.model

data class SensorReading(
    val values: List<Double>,
    val unit: String,
    val sensorType: SensorType = SensorType.UNKNOWN
) {
    val formattedValue: String
        get() = if (values.size == 1) {
            "%.6f %s".format(values[0], unit)
        } else {
            values.joinToString("\n") { "%.6f".format(it) } + " " + unit
        }

    val copyableValue: String
        get() = when (sensorType) {
            // GPS: longitude, latitude format
            SensorType.GPS -> {
                if (values.size >= 2) {
                    "%.6f, %.6f".format(values[0], values[1])
                } else {
                    values.joinToString(", ") { "%.6f".format(it) } + " " + unit
                }
            }
            // Single value: just number and unit
            else -> {
                if (values.size == 1) {
                    "%.6f %s".format(values[0], unit)
                } else {
                    values.joinToString(", ") { "%.6f".format(it) } + " " + unit
                }
            }
        }
}
