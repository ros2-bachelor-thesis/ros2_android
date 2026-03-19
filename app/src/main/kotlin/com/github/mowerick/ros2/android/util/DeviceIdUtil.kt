package com.github.mowerick.ros2.android.util

import android.content.Context
import android.os.Build
import android.provider.Settings

fun getDefaultDeviceId(context: Context): String {
    return try {
        // Try to get device name from Settings (API 25+)
        val deviceName = Settings.Global.getString(
            context.contentResolver,
            Settings.Global.DEVICE_NAME
        )
        if (!deviceName.isNullOrBlank()) {
            sanitizeDeviceId(deviceName)
        } else {
            // Fallback to Build.MODEL
            sanitizeDeviceId(Build.MODEL)
        }
    } catch (e: Exception) {
        // Final fallback
        sanitizeDeviceId(Build.MODEL)
    }
}

fun sanitizeDeviceId(id: String): String {
    // Convert to lowercase, replace spaces and special chars with underscores
    return id.trim()
        .lowercase()
        .replace(Regex("[^a-z0-9]+"), "_")
        .removePrefix("_")
        .removeSuffix("_")
        .takeIf { it.isNotEmpty() } ?: "android_device"
}
