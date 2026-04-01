package com.github.mowerick.ros2.android.viewmodel.managers

import android.graphics.Bitmap
import com.github.mowerick.ros2.android.util.NativeBridge
import com.github.mowerick.ros2.android.model.CameraInfo
import com.github.mowerick.ros2.android.model.SensorInfo
import com.github.mowerick.ros2.android.model.SensorReading
import com.github.mowerick.ros2.android.model.SensorType
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.launch

/**
 * Manages built-in sensors and cameras (Android sensors, USB cameras via libusb)
 */
class SensorCameraManager(
    private val coroutineScope: CoroutineScope,
    private val isLocationServiceEnabled: () -> Boolean
) {

    private val _sensors = MutableStateFlow<List<SensorInfo>>(emptyList())
    val sensors: StateFlow<List<SensorInfo>> = _sensors

    private val _cameras = MutableStateFlow<List<CameraInfo>>(emptyList())
    val cameras: StateFlow<List<CameraInfo>> = _cameras

    private val _currentReading = MutableStateFlow<SensorReading?>(null)
    val currentReading: StateFlow<SensorReading?> = _currentReading

    private val _cameraFrame = MutableStateFlow<Bitmap?>(null)
    val cameraFrame: StateFlow<Bitmap?> = _cameraFrame

    fun refreshSensorsAndCameras() {
        refreshSensors()
        refreshCameras()
    }

    fun refreshSensors() {
        try {
            val sensors = NativeBridge.nativeGetSensorList()
            _sensors.value = sensors.toList()
        } catch (e: Exception) {
            android.util.Log.e("SensorCameraManager", "Failed to refresh sensors", e)
        }
    }

    fun refreshCameras() {
        try {
            val cameras = NativeBridge.nativeGetCameraList()
            _cameras.value = cameras.toList()
        } catch (e: Exception) {
            android.util.Log.e("SensorCameraManager", "Failed to refresh cameras", e)
        }
    }

    fun enableCamera(uniqueId: String) {
        NativeBridge.nativeEnableCamera(uniqueId)
        refreshCameras()
    }

    fun disableCamera(uniqueId: String) {
        _cameraFrame.value = null
        NativeBridge.nativeDisableCamera(uniqueId)
        refreshCameras()
    }

    fun enableSensor(uniqueId: String) {
        NativeBridge.nativeEnableSensor(uniqueId)
        refreshSensors()
    }

    fun disableSensor(uniqueId: String) {
        NativeBridge.nativeDisableSensor(uniqueId)
        refreshSensors()
    }

    fun updateSensorReading(sensorId: String) {
        coroutineScope.launch {
            try {
                var reading = NativeBridge.nativeGetSensorData(sensorId)

                // Override GPS reading with status message if location services disabled
                if (sensorId == "gps_location_provider" && !isLocationServiceEnabled()) {
                    reading = SensorReading(
                        values = emptyList(),
                        unit = "",
                        sensorType = SensorType.GPS,
                        statusMessage = "Location services disabled"
                    )
                }

                _currentReading.value = reading
            } catch (e: Exception) {
                android.util.Log.e("SensorCameraManager", "Failed to get sensor data for $sensorId", e)
            }
        }
    }

    fun updateCameraFrame(cameraId: String) {
        coroutineScope.launch {
            try {
                val bitmap = NativeBridge.nativeGetCameraFrame(cameraId)
                _cameraFrame.value = bitmap
            } catch (e: Exception) {
                android.util.Log.e("SensorCameraManager", "Failed to get camera frame for $cameraId", e)
            }
        }
    }

    fun clearSensorReading() {
        _currentReading.value = null
    }

    fun clearCameraFrame() {
        _cameraFrame.value = null
    }
}
