package com.github.sloretz.sensors_for_ros

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
    external fun nativeGetNetworkInterfaces(): String
}
