package com.github.mowerick.ros2.android

import android.graphics.Bitmap
import com.github.mowerick.ros2.android.model.CameraInfo
import com.github.mowerick.ros2.android.model.ExternalDeviceInfo
import com.github.mowerick.ros2.android.model.SensorInfo
import com.github.mowerick.ros2.android.model.SensorReading

object NativeBridge {
    init {
        System.loadLibrary("android-ros")
    }

    private var notificationCallback: ((String, String) -> Unit)? = null
    private var gpsEnableCallback: (() -> Unit)? = null
    private var gpsDisableCallback: (() -> Unit)? = null
    private var sensorDataCallback: ((String) -> Unit)? = null
    private var cameraFrameCallback: ((String) -> Unit)? = null

    external fun nativeInit(cacheDir: String, packageName: String)
    external fun nativeDestroy()
    external fun nativeSetNetworkInterfaces(interfaces: Array<String>)
    external fun nativeOnPermissionResult(permission: String, granted: Boolean)

    external fun nativeStartRos(domainId: Int, networkInterface: String, deviceId: String)
    external fun nativeGetSensorList(): Array<SensorInfo>
    external fun nativeGetSensorData(uniqueId: String): SensorReading?
    external fun nativeGetCameraList(): Array<CameraInfo>
    external fun nativeEnableCamera(uniqueId: String)
    external fun nativeDisableCamera(uniqueId: String)
    external fun nativeEnableSensor(uniqueId: String)
    external fun nativeDisableSensor(uniqueId: String)
    external fun nativeGetNetworkInterfaces(): Array<String>
    external fun nativeGetDiscoveredTopics(): Array<String>
    external fun nativeGetCameraFrame(uniqueId: String): Bitmap?
    external fun nativeSetNotificationCallback()
    external fun nativeSetSensorDataCallback()
    external fun nativeSetCameraFrameCallback()
    external fun nativeOnGpsLocation(
        latitude: Double,
        longitude: Double,
        altitude: Double,
        accuracy: Float,
        altitudeAccuracy: Float,
        timestampNs: Long
    )

    // LIDAR device management
    external fun nativeConnectLidar(ttyPath: String, uniqueId: String, baudrate: Int): Boolean
    external fun nativeDisconnectLidar(uniqueId: String): Boolean
    external fun nativeGetLidarList(): Array<ExternalDeviceInfo>
    external fun nativeEnableLidar(uniqueId: String): Boolean
    external fun nativeDisableLidar(uniqueId: String): Boolean

    // Perception (Object Detection) management
    external fun enablePerception(modelsPath: String)
    external fun disablePerception()
    external fun isPerceptionEnabled(): Boolean
    external fun getPerceptionStats(): String

    fun setNotificationCallback(callback: (severity: String, message: String) -> Unit) {
        notificationCallback = callback
        nativeSetNotificationCallback()
    }

    fun setGpsCallbacks(onEnable: () -> Unit, onDisable: () -> Unit) {
        gpsEnableCallback = onEnable
        gpsDisableCallback = onDisable
    }

    fun setSensorDataCallback(callback: (sensorId: String) -> Unit) {
        sensorDataCallback = callback
        nativeSetSensorDataCallback()
    }

    fun setCameraFrameCallback(callback: (cameraId: String) -> Unit) {
        cameraFrameCallback = callback
        nativeSetCameraFrameCallback()
    }

    // Called from native code (JNI)
    @Suppress("unused")
    @JvmStatic
    private fun onNotification(severity: String, message: String) {
        notificationCallback?.invoke(severity, message)
    }

    // Called from native code when GPS sensor is enabled
    @Suppress("unused")
    @JvmStatic
    private fun onGpsEnable() {
        gpsEnableCallback?.invoke()
    }

    // Called from native code when GPS sensor is disabled
    @Suppress("unused")
    @JvmStatic
    private fun onGpsDisable() {
        gpsDisableCallback?.invoke()
    }

    // Called from native code when sensor data is updated
    @Suppress("unused")
    @JvmStatic
    private fun onSensorDataUpdate(sensorId: String) {
        sensorDataCallback?.invoke(sensorId)
    }

    // Called from native code when camera frame is updated
    @Suppress("unused")
    @JvmStatic
    private fun onCameraFrameUpdate(cameraId: String) {
        cameraFrameCallback?.invoke(cameraId)
    }
}
