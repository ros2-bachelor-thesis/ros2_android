package com.github.mowerick.ros2.android.model

enum class SensorType {
    ACCELEROMETER,
    BAROMETER,
    GPS,
    GYROSCOPE,
    ILLUMINANCE,
    MAGNETOMETER,
    UNKNOWN;

    companion object {
        @JvmStatic
        fun fromString(value: String): SensorType {
            return when (value.lowercase()) {
                "accelerometer" -> ACCELEROMETER
                "barometer" -> BAROMETER
                "gps" -> GPS
                "gyroscope" -> GYROSCOPE
                "illuminance" -> ILLUMINANCE
                "magnetometer" -> MAGNETOMETER
                else -> UNKNOWN
            }
        }
    }
}
