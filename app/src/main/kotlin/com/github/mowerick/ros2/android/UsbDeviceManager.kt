package com.github.mowerick.ros2.android

import android.app.PendingIntent
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.hardware.usb.UsbDevice
import android.hardware.usb.UsbDeviceConnection
import android.hardware.usb.UsbManager
import android.os.Build
import android.util.Log
import com.github.mowerick.ros2.android.model.ExternalDeviceInfo
import com.github.mowerick.ros2.android.model.ExternalDeviceType

class UsbDeviceManager(private val context: Context) {

    private val usbManager = context.getSystemService(Context.USB_SERVICE) as UsbManager
    private var connection: UsbDeviceConnection? = null
    private var permissionCallback: ((Boolean) -> Unit)? = null

    private val permissionReceiver = object : BroadcastReceiver() {
        override fun onReceive(context: Context, intent: Intent) {
            if (intent.action == ACTION_USB_PERMISSION) {
                synchronized(this) {
                    val device = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
                        intent.getParcelableExtra(UsbManager.EXTRA_DEVICE, UsbDevice::class.java)
                    } else {
                        @Suppress("DEPRECATION")
                        intent.getParcelableExtra(UsbManager.EXTRA_DEVICE)
                    }

                    val granted = intent.getBooleanExtra(UsbManager.EXTRA_PERMISSION_GRANTED, false)

                    if (device != null) {
                        Log.i(TAG, "USB permission result for ${device.deviceName}: $granted")
                        permissionCallback?.invoke(granted)
                        permissionCallback = null
                    }
                }
            }
        }
    }

    init {
        // Register permission receiver
        val filter = IntentFilter(ACTION_USB_PERMISSION)
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            context.registerReceiver(permissionReceiver, filter, Context.RECEIVER_NOT_EXPORTED)
        } else {
            context.registerReceiver(permissionReceiver, filter)
        }
    }

    /**
     * Detect YDLIDAR devices connected to the system
     * Common YDLIDAR USB-to-Serial chips: CP210x (0x10c4:0xea60), CH340 (0x1a86:0x7523)
     */
    fun detectLidarDevices(): List<UsbDevice> {
        val deviceList = usbManager.deviceList
        return deviceList.values.filter { device ->
            // Check for common USB-to-Serial chips used by YDLIDAR
            (device.vendorId == 0x10c4 && device.productId == 0xea60) ||  // CP210x
            (device.vendorId == 0x1a86 && device.productId == 0x7523)     // CH340
        }
    }

    /**
     * Convert UsbDevice to ExternalDeviceInfo for UI display
     */
    fun deviceToInfo(device: UsbDevice, connected: Boolean = false, enabled: Boolean = false): ExternalDeviceInfo {
        val deviceName = when {
            device.vendorId == 0x10c4 && device.productId == 0xea60 -> "YDLIDAR (CP210x)"
            device.vendorId == 0x1a86 && device.productId == 0x7523 -> "YDLIDAR (CH340)"
            else -> "YDLIDAR"
        }

        return ExternalDeviceInfo(
            uniqueId = "ydlidar_${device.deviceName.replace("/", "_")}",
            name = deviceName,
            deviceType = ExternalDeviceType.LIDAR,
            usbPath = device.deviceName,
            vendorId = device.vendorId,
            productId = device.productId,
            topicName = "/scan",
            topicType = "sensor_msgs/msg/LaserScan",
            connected = connected,
            enabled = enabled
        )
    }

    /**
     * Request USB permission for a device
     * Callback is invoked with permission result
     */
    fun requestPermission(device: UsbDevice, callback: (Boolean) -> Unit) {
        if (usbManager.hasPermission(device)) {
            callback(true)
            return
        }

        permissionCallback = callback

        val permissionIntent = PendingIntent.getBroadcast(
            context,
            0,
            Intent(ACTION_USB_PERMISSION),
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
                PendingIntent.FLAG_MUTABLE
            } else {
                0
            }
        )

        usbManager.requestPermission(device, permissionIntent)
        Log.i(TAG, "Requesting USB permission for ${device.deviceName}")
    }

    /**
     * Open USB device and return file descriptor + device path
     * Returns null if device cannot be opened
     */
    fun openDevice(device: UsbDevice): Pair<Int, String>? {
        if (!usbManager.hasPermission(device)) {
            Log.w(TAG, "Cannot open device ${device.deviceName}: no permission")
            return null
        }

        closeDevice()  // Close any existing connection

        connection = usbManager.openDevice(device)
        if (connection == null) {
            Log.e(TAG, "Failed to open USB device ${device.deviceName}")
            return null
        }

        val fd = connection!!.fileDescriptor
        val devicePath = device.deviceName

        Log.i(TAG, "Opened USB device: fd=$fd, path=$devicePath")
        return Pair(fd, devicePath)
    }

    /**
     * Close current USB device connection
     */
    fun closeDevice() {
        connection?.close()
        connection = null
    }

    /**
     * Check if manager has permission for device
     */
    fun hasPermission(device: UsbDevice): Boolean {
        return usbManager.hasPermission(device)
    }

    /**
     * Cleanup receiver on destroy
     */
    fun destroy() {
        try {
            context.unregisterReceiver(permissionReceiver)
        } catch (e: Exception) {
            // Already unregistered
        }
        closeDevice()
    }

    companion object {
        private const val TAG = "UsbDeviceManager"
        const val ACTION_USB_PERMISSION = "com.github.mowerick.ros2.android.USB_PERMISSION"
    }
}
