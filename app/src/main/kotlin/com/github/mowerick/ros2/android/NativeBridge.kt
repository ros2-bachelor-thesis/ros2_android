package com.github.mowerick.ros2.android

import android.graphics.Bitmap
import com.github.mowerick.ros2.android.model.CameraInfo
import com.github.mowerick.ros2.android.model.SensorInfo
import com.github.mowerick.ros2.android.model.SensorReading

object NativeBridge {
    init {
        System.loadLibrary("android-ros")
    }

    private var notificationCallback: ((String, String) -> Unit)? = null
    private var gpsEnableCallback: (() -> Unit)? = null
    private var gpsDisableCallback: (() -> Unit)? = null

    external fun nativeInit(cacheDir: String, packageName: String)
    external fun nativeDestroy()
    external fun nativeSetNetworkInterfaces(interfaces: Array<String>)
    external fun nativeOnPermissionResult(permission: String, granted: Boolean)

    external fun nativeStartRos(domainId: Int, networkInterface: String)
    external fun nativeStopRos()
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
    external fun nativeGetPendingNotifications(): String
    external fun nativeSetNotificationCallback()
    external fun nativeOnGpsLocation(
        latitude: Double,
        longitude: Double,
        altitude: Double,
        accuracy: Float,
        altitudeAccuracy: Float,
        timestampNs: Long
    )

    fun setNotificationCallback(callback: (severity: String, message: String) -> Unit) {
        notificationCallback = callback
        nativeSetNotificationCallback()
    }

    fun setGpsCallbacks(onEnable: () -> Unit, onDisable: () -> Unit) {
        gpsEnableCallback = onEnable
        gpsDisableCallback = onDisable
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
}
