package com.github.sloretz.sensors_for_ros.viewmodel

import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.github.sloretz.sensors_for_ros.NativeBridge
import com.github.sloretz.sensors_for_ros.model.CameraInfo
import com.github.sloretz.sensors_for_ros.model.SensorInfo
import com.github.sloretz.sensors_for_ros.model.SensorReading
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.launch
import org.json.JSONArray
import org.json.JSONObject

sealed class Screen {
    data object DomainId : Screen()
    data object SensorList : Screen()
    data class SensorDetail(val sensor: SensorInfo) : Screen()
    data class CameraDetail(val camera: CameraInfo) : Screen()
}

class RosViewModel : ViewModel() {

    private val _screen = MutableStateFlow<Screen>(Screen.DomainId)
    val screen: StateFlow<Screen> = _screen

    private val _sensors = MutableStateFlow<List<SensorInfo>>(emptyList())
    val sensors: StateFlow<List<SensorInfo>> = _sensors

    private val _cameras = MutableStateFlow<List<CameraInfo>>(emptyList())
    val cameras: StateFlow<List<CameraInfo>> = _cameras

    private val _currentReading = MutableStateFlow<SensorReading?>(null)
    val currentReading: StateFlow<SensorReading?> = _currentReading

    private val _networkInterfaces = MutableStateFlow<List<String>>(emptyList())
    val networkInterfaces: StateFlow<List<String>> = _networkInterfaces

    private var polling = false

    fun loadNetworkInterfaces() {
        try {
            val json = NativeBridge.nativeGetNetworkInterfaces()
            val arr = JSONArray(json)
            val ifaces = mutableListOf<String>()
            for (i in 0 until arr.length()) {
                ifaces.add(arr.getString(i))
            }
            _networkInterfaces.value = ifaces
        } catch (_: Exception) {}
    }

    fun startRos(domainId: Int, networkInterface: String) {
        NativeBridge.nativeStartRos(domainId, networkInterface)
        refreshSensorsAndCameras()
        _screen.value = Screen.SensorList
    }

    fun navigateToSensor(sensor: SensorInfo) {
        _currentReading.value = null
        _screen.value = Screen.SensorDetail(sensor)
        startPolling(sensor.uniqueId)
    }

    fun navigateToCamera(camera: CameraInfo) {
        _screen.value = Screen.CameraDetail(camera)
    }

    fun navigateBack() {
        stopPolling()
        when (_screen.value) {
            is Screen.SensorDetail, is Screen.CameraDetail -> {
                refreshSensorsAndCameras()
                _screen.value = Screen.SensorList
            }
            is Screen.SensorList -> {
                _screen.value = Screen.DomainId
            }
            else -> {}
        }
    }

    fun enableCamera(uniqueId: String) {
        NativeBridge.nativeEnableCamera(uniqueId)
        refreshCameras()
        updateCameraDetailScreen(uniqueId)
    }

    fun disableCamera(uniqueId: String) {
        NativeBridge.nativeDisableCamera(uniqueId)
        refreshCameras()
        updateCameraDetailScreen(uniqueId)
    }

    private fun updateCameraDetailScreen(uniqueId: String) {
        val current = _screen.value
        if (current is Screen.CameraDetail && current.camera.uniqueId == uniqueId) {
            val updated = _cameras.value.find { it.uniqueId == uniqueId }
            if (updated != null) {
                _screen.value = Screen.CameraDetail(updated)
            }
        }
    }

    private fun refreshSensorsAndCameras() {
        refreshSensors()
        refreshCameras()
    }

    private fun refreshSensors() {
        try {
            val json = NativeBridge.nativeGetSensorList()
            val arr = JSONArray(json)
            val list = mutableListOf<SensorInfo>()
            for (i in 0 until arr.length()) {
                val obj = arr.getJSONObject(i)
                list.add(
                    SensorInfo(
                        uniqueId = obj.getString("uniqueId"),
                        prettyName = obj.getString("prettyName"),
                        sensorName = obj.getString("sensorName"),
                        vendor = obj.getString("vendor"),
                        topicName = obj.getString("topicName"),
                        topicType = obj.getString("topicType")
                    )
                )
            }
            _sensors.value = list
        } catch (_: Exception) {}
    }

    private fun refreshCameras() {
        try {
            val json = NativeBridge.nativeGetCameraList()
            val arr = JSONArray(json)
            val list = mutableListOf<CameraInfo>()
            for (i in 0 until arr.length()) {
                val obj = arr.getJSONObject(i)
                list.add(
                    CameraInfo(
                        uniqueId = obj.getString("uniqueId"),
                        name = obj.getString("name"),
                        enabled = obj.getBoolean("enabled"),
                        imageTopicName = obj.getString("imageTopicName"),
                        imageTopicType = obj.getString("imageTopicType"),
                        infoTopicName = obj.getString("infoTopicName"),
                        infoTopicType = obj.getString("infoTopicType"),
                        resolutionWidth = obj.getInt("resolutionWidth"),
                        resolutionHeight = obj.getInt("resolutionHeight")
                    )
                )
            }
            _cameras.value = list
        } catch (_: Exception) {}
    }

    private fun startPolling(uniqueId: String) {
        polling = true
        viewModelScope.launch {
            while (polling) {
                try {
                    val json = NativeBridge.nativeGetSensorData(uniqueId)
                    val obj = JSONObject(json)
                    if (obj.has("values")) {
                        val valuesArr = obj.getJSONArray("values")
                        val values = mutableListOf<Double>()
                        for (i in 0 until valuesArr.length()) {
                            values.add(valuesArr.getDouble(i))
                        }
                        _currentReading.value = SensorReading(
                            values = values,
                            unit = obj.getString("unit")
                        )
                    }
                } catch (_: Exception) {}
                delay(100)
            }
        }
    }

    private fun stopPolling() {
        polling = false
        _currentReading.value = null
    }
}
