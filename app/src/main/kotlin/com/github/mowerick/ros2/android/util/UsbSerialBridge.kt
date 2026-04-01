package com.github.mowerick.ros2.android.util

import android.util.Log
import com.github.mowerick.ros2.android.viewmodel.managers.UsbSerialManager
import java.util.concurrent.ConcurrentHashMap

/**
 * JNI bridge for USB serial communication.
 *
 * This object provides static methods called from native C++ code (android_jni_serial.cpp)
 * to open/close USB serial devices and manage BufferedUsbSerialPort instances.
 *
 * Architecture:
 * ```
 * C++ (YDLIDAR SDK)
 *   ↓ JNI calls
 * UsbSerialBridge (this class)
 *   ↓ device management
 * BufferedUsbSerialPort
 *   ↓ USB operations
 * UsbSerialPort (mik3y library)
 * ```
 *
 * Thread Safety:
 * - All methods are thread-safe via ConcurrentHashMap
 * - Multiple native threads can call these methods concurrently
 *
 * Object Lifetime:
 * - C++ code holds global references to BufferedUsbSerialPort instances
 * - Native code must call closeDevice() to release resources
 */
object UsbSerialBridge {

    private const val TAG = "UsbSerialBridge"

    // Active device connections
    // Key: device ID (e.g., "lidar_dev_bus_001_device_003")
    // Value: BufferedUsbSerialPort instance
    private val activeDevices = ConcurrentHashMap<String, BufferedUsbSerialPort>()

    // UsbSerialManager instance (set by RosViewModel)
    @Volatile
    private var usbSerialManager: UsbSerialManager? = null

    /**
     * Initialize UsbSerialManager
     *
     * Must be called from RosViewModel before any device operations.
     *
     * @param manager UsbSerialManager instance
     */
    fun setUsbSerialManager(manager: UsbSerialManager) {
        usbSerialManager = manager
        Log.i(TAG, "UsbSerialManager initialized")
    }

    /**
     * Open USB serial device (called from native code via JNI)
     *
     * This method is called by android_jni_serial.cpp when the YDLIDAR SDK
     * calls Serial::open().
     *
     * @param deviceId Device identifier (e.g., "lidar_dev_bus_001_device_003")
     * @param baudrate Baud rate (e.g., 512000 for YDLIDAR TG15)
     * @param dataBits Data bits (5/6/7/8)
     * @param stopBits Stop bits (0=1 bit, 1=1.5 bits, 2=2 bits)
     * @param parity Parity (0=none, 1=odd, 2=even)
     * @return BufferedUsbSerialPort instance or null if open failed
     */
    @JvmStatic
    fun openDevice(
        deviceId: String,
        baudrate: Int,
        dataBits: Int,
        stopBits: Int,
        parity: Int
    ): BufferedUsbSerialPort? {
        Log.i(TAG, "Opening device: $deviceId (baud=$baudrate, data=$dataBits, stop=$stopBits, parity=$parity)")

        val manager = usbSerialManager
        if (manager == null) {
            Log.e(TAG, "UsbSerialManager not initialized")
            return null
        }

        // Convert USB path to uniqueId format
        // Native code passes: "/dev/bus/usb/001/002"
        // We need:           "lidar__dev_bus_usb_001_002"
        val uniqueId = "lidar_${deviceId.replace("/", "_")}"
        Log.d(TAG, "Converted path '$deviceId' to uniqueId '$uniqueId'")

        // Check if already open
        activeDevices[uniqueId]?.let {
            Log.w(TAG, "Device already open: $uniqueId")
            return it
        }

        // Connect to USB device
        val port = manager.connectDevice(uniqueId, baudrate)
        if (port == null) {
            Log.e(TAG, "Failed to connect to device: $uniqueId (path=$deviceId)")
            return null
        }

        // Note: setParameters() is already called by UsbSerialManager.connectDevice()
        // The dataBits/stopBits/parity parameters here are for logging/verification only

        // Wrap in buffered wrapper
        val buffered = try {
            BufferedUsbSerialPort(port)
        } catch (e: Exception) {
            Log.e(TAG, "Failed to create BufferedUsbSerialPort for $uniqueId: ${e.message}", e)
            try {
                port.close()
            } catch (closeError: Exception) {
                Log.w(TAG, "Error closing port after buffer creation failed: ${closeError.message}")
            }
            return null
        }

        // Store in active devices map
        activeDevices[uniqueId] = buffered

        Log.i(TAG, "Device opened successfully: $uniqueId (path=$deviceId)")
        return buffered
    }

    /**
     * Close USB serial device (called from native code via JNI)
     *
     * This method is called by android_jni_serial.cpp when the YDLIDAR SDK
     * calls Serial::close().
     *
     * @param deviceId Device identifier
     */
    @JvmStatic
    fun closeDevice(deviceId: String) {
        Log.i(TAG, "Closing device: $deviceId")

        // Convert path to uniqueId (same as openDevice)
        val uniqueId = "lidar_${deviceId.replace("/", "_")}"

        val buffered = activeDevices.remove(uniqueId)
        if (buffered != null) {
            try {
                buffered.close()
                Log.i(TAG, "Device closed successfully: $uniqueId (path=$deviceId)")
            } catch (e: Exception) {
                Log.e(TAG, "Error closing device $uniqueId: ${e.message}", e)
            }
        } else {
            Log.w(TAG, "Device not found in active devices: $uniqueId (path=$deviceId)")
        }

        // Also clean up UsbSerialManager's activeConnections map
        // BufferedUsbSerialPort.close() already closed the underlying UsbSerialPort,
        // so we just need to remove the stale reference from UsbSerialManager's map
        val manager = usbSerialManager
        if (manager != null) {
            manager.disconnectDevice(uniqueId, closePort = false)
        } else {
            Log.w(TAG, "UsbSerialManager not available to clean up connection for: $uniqueId")
        }
    }

    /**
     * Get active device (for debugging)
     *
     * @param deviceId Device identifier
     * @return BufferedUsbSerialPort instance or null
     */
    fun getDevice(deviceId: String): BufferedUsbSerialPort? {
        return activeDevices[deviceId]
    }

    /**
     * Get list of active device IDs (for debugging)
     */
    fun getActiveDeviceIds(): List<String> {
        return activeDevices.keys.toList()
    }

    /**
     * Initialize JNI method IDs (called from native code)
     *
     * This is called once during app initialization to cache Java method IDs
     * for fast JNI calls from C++.
     *
     * Implementation is in src/jni/jni_bridge.cc
     */
    external fun nativeInitJNI()
}
