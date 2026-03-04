package com.github.sloretz.sensors_for_ros

import android.view.Surface

object NativeBridge {
    init {
        System.loadLibrary("android-ros")
    }

    external fun nativeInit(cacheDir: String, packageName: String)
    external fun nativeSurfaceCreated(surface: Surface)
    external fun nativeSurfaceDestroyed()
    external fun nativeTouchEvent(action: Int, x: Float, y: Float, toolType: Int)
    external fun nativeDestroy()
    external fun nativeSetNetworkInterfaces(interfaces: Array<String>)
    external fun nativeOnPermissionResult(permission: String, granted: Boolean)
}
