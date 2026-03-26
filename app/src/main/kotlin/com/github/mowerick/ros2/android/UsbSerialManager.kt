package com.github.mowerick.ros2.android

import android.app.PendingIntent
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.hardware.usb.UsbDevice
import android.hardware.usb.UsbManager
import android.os.Build
import android.util.Log
import com.github.mowerick.ros2.android.model.ExternalDeviceInfo
import com.github.mowerick.ros2.android.model.ExternalDeviceType
import com.hoho.android.usbserial.driver.UsbSerialDriver
import com.hoho.android.usbserial.driver.UsbSerialPort
import com.hoho.android.usbserial.driver.UsbSerialProber

/**
 * Manages USB serial device detection and connection using mik3y/usb-serial-for-android library.
 *
 * This manager provides:
 * - Detection of USB serial devices (CP210x, FTDI, PL2303, CH34x)
 * - Connection management for YDLIDAR devices
 * - Port configuration (baud rate, data bits, etc.)
 * - Direct access to UsbSerialPort for JNI bridge
 *
 * The UsbSerialPort instance is passed to native code via JNI, where a custom serial
 * backend routes all YDLIDAR SDK operations through Java USB Serial library.
 */
class UsbSerialManager(private val context: Context) {

    private val usbManager: UsbManager = context.getSystemService(Context.USB_SERVICE) as UsbManager
    private val activeConnections = mutableMapOf<String, UsbSerialPort>()

    companion object {
        private const val TAG = "UsbSerialManager"
        private const val ACTION_USB_PERMISSION = "com.github.mowerick.ros2.android.USB_PERMISSION"
    }

    /**
     * Detect all connected USB serial devices
     *
     * @return List of detected YDLIDAR devices (currently filters for CP210x chips)
     */
    fun detectLidarDevices(): List<ExternalDeviceInfo> {
        Log.i(TAG, "Scanning for USB serial devices...")

        val availableDrivers = UsbSerialProber.getDefaultProber().findAllDrivers(usbManager)
        Log.i(TAG, "Found ${availableDrivers.size} USB serial driver(s)")

        val lidarDevices = mutableListOf<ExternalDeviceInfo>()

        for (driver in availableDrivers) {
            val device = driver.device
            val vendorId = device.vendorId
            val productId = device.productId

            Log.d(TAG, "USB device: VID=${String.format("0x%04x", vendorId)}, " +
                    "PID=${String.format("0x%04x", productId)}, " +
                    "deviceName=${device.deviceName}, " +
                    "ports=${driver.ports.size}")

            // Check if this is a known YDLIDAR device (CP210x or CH340)
            val isYdlidar = isYdLidarDevice(vendorId, productId)

            if (isYdlidar) {
                val uniqueId = "lidar_${device.deviceName.replace("/", "_")}"
                val deviceInfo = ExternalDeviceInfo(
                    uniqueId = uniqueId,
                    name = getDeviceName(vendorId, productId),
                    deviceType = ExternalDeviceType.LIDAR,
                    usbPath = device.deviceName,
                    vendorId = vendorId,
                    productId = productId,
                    topicName = "/scan",
                    topicType = "sensor_msgs/msg/LaserScan",
                    connected = activeConnections.containsKey(uniqueId),
                    enabled = false  // Will be updated by ROS system
                )
                lidarDevices.add(deviceInfo)
                Log.i(TAG, "Detected YDLIDAR: $deviceInfo")
            } else {
                Log.d(TAG, "Skipping non-YDLIDAR device: VID=${String.format("0x%04x", vendorId)}, " +
                        "PID=${String.format("0x%04x", productId)}")
            }
        }

        if (lidarDevices.isEmpty()) {
            Log.w(TAG, "No YDLIDAR devices detected")
        } else {
            Log.i(TAG, "Found ${lidarDevices.size} YDLIDAR device(s)")
        }

        return lidarDevices
    }

    /**
     * Connect to a USB serial device and configure the port
     *
     * @param uniqueId Device unique ID (from detectLidarDevices)
     * @param baudRate Baud rate (default: 512000 for TG15)
     * @return UsbSerialPort instance if successful, null otherwise
     */
    fun connectDevice(uniqueId: String, baudRate: Int = 512000): UsbSerialPort? {
        Log.i(TAG, "Connecting to device: $uniqueId at $baudRate baud")

        // Check if already connected
        activeConnections[uniqueId]?.let {
            Log.w(TAG, "Device already connected: $uniqueId")
            return it
        }

        // Find the USB device
        val availableDrivers = UsbSerialProber.getDefaultProber().findAllDrivers(usbManager)
        val driver = availableDrivers.find { driver ->
            val deviceId = "lidar_${driver.device.deviceName.replace("/", "_")}"
            deviceId == uniqueId
        }

        if (driver == null) {
            Log.e(TAG, "Device not found: $uniqueId")
            return null
        }

        // Check USB permissions
        val device = driver.device
        if (!usbManager.hasPermission(device)) {
            Log.w(TAG, "No USB permission for device: ${device.deviceName}, requesting permission")
            requestPermission(device)
            return null
        }

        // Open connection
        val connection = usbManager.openDevice(device)
        if (connection == null) {
            Log.e(TAG, "Failed to open USB connection for: ${device.deviceName}")
            return null
        }

        // Get first port (YDLIDAR devices have single port)
        val port = driver.ports[0]

        try {
            // Open port
            port.open(connection)

            // Configure port for YDLIDAR TG15
            // 8N1: 8 data bits, no parity, 1 stop bit
            port.setParameters(
                baudRate,
                8,  // data bits
                UsbSerialPort.STOPBITS_1,
                UsbSerialPort.PARITY_NONE
            )

            // Disable flow control (YDLIDAR doesn't use it)
            port.dtr = false
            port.rts = false

            activeConnections[uniqueId] = port
            Log.i(TAG, "Successfully connected to $uniqueId at $baudRate baud")

            return port
        } catch (e: Exception) {
            Log.e(TAG, "Failed to configure port for $uniqueId: ${e.message}", e)
            try {
                port.close()
            } catch (closeException: Exception) {
                Log.w(TAG, "Error closing port after failed configuration: ${closeException.message}")
            }
            return null
        }
    }

    /**
     * Disconnect from a USB serial device
     *
     * @param uniqueId Device unique ID
     */
    fun disconnectDevice(uniqueId: String) {
        val port = activeConnections.remove(uniqueId)
        if (port != null) {
            try {
                port.close()
                Log.i(TAG, "Disconnected from device: $uniqueId")
            } catch (e: Exception) {
                Log.e(TAG, "Error disconnecting from $uniqueId: ${e.message}", e)
            }
        } else {
            Log.w(TAG, "Device not connected: $uniqueId")
        }
    }

    /**
     * Get active port for a device (used by JNI bridge)
     *
     * @param uniqueId Device unique ID
     * @return UsbSerialPort instance if connected, null otherwise
     */
    fun getPort(uniqueId: String): UsbSerialPort? {
        return activeConnections[uniqueId]
    }

    /**
     * Check if device is a known YDLIDAR USB-to-serial chip
     *
     * @param vendorId USB vendor ID
     * @param productId USB product ID
     * @return true if known YDLIDAR chip
     */
    private fun isYdLidarDevice(vendorId: Int, productId: Int): Boolean {
        // CP210x (Silicon Labs) - most common in YDLIDAR
        if (vendorId == 0x10c4 && productId == 0xea60) {
            return true
        }

        // CH340/CH341 (Jiangsu QinHeng) - alternative chip
        if (vendorId == 0x1a86 && (productId == 0x7523 || productId == 0x5523)) {
            return true
        }

        return false
    }

    /**
     * Get human-readable device name based on VID/PID
     */
    private fun getDeviceName(vendorId: Int, productId: Int): String {
        return when {
            vendorId == 0x10c4 && productId == 0xea60 -> "YDLIDAR (CP2102)"
            vendorId == 0x1a86 && productId == 0x7523 -> "YDLIDAR (CH340)"
            vendorId == 0x1a86 && productId == 0x5523 -> "YDLIDAR (CH341)"
            else -> "YDLIDAR (Unknown)"
        }
    }

    /**
     * Request USB permission for a device
     *
     * This triggers a system dialog asking the user to grant USB access.
     * The result will be broadcast to ACTION_USB_PERMISSION.
     */
    fun requestPermission(device: UsbDevice) {
        val flags = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            PendingIntent.FLAG_MUTABLE
        } else {
            0
        }

        val permissionIntent = PendingIntent.getBroadcast(
            context,
            0,
            Intent(ACTION_USB_PERMISSION),
            flags
        )

        Log.i(TAG, "Requesting USB permission for device: ${device.deviceName}")
        usbManager.requestPermission(device, permissionIntent)
    }
}
