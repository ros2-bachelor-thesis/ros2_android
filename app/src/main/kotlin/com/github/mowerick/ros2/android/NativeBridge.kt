package com.github.mowerick.ros2.android

object NativeBridge {
    init {
        System.loadLibrary("android-ros")
    }

    external fun nativeInit(cacheDir: String, packageName: String)
    external fun nativeDestroy()
    external fun nativeSetNetworkInterfaces(interfaces: Array<String>)
    external fun nativeOnPermissionResult(permission: String, granted: Boolean)

    external fun nativeStartRos(domainId: Int, networkInterface: String)
    external fun nativeGetSensorList(): String
    external fun nativeGetSensorData(uniqueId: String): String
    external fun nativeGetCameraList(): String
    external fun nativeEnableCamera(uniqueId: String)
    external fun nativeDisableCamera(uniqueId: String)
    external fun nativeEnableSensor(uniqueId: String)
    external fun nativeDisableSensor(uniqueId: String)
    external fun nativeGetNetworkInterfaces(): String
}
