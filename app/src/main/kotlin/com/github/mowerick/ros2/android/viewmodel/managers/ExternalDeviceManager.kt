package com.github.mowerick.ros2.android.viewmodel.managers

import android.hardware.usb.UsbDevice
import com.github.mowerick.ros2.android.util.NativeBridge
import com.github.mowerick.ros2.android.model.ExternalDeviceInfo
import com.github.mowerick.ros2.android.model.ExternalDeviceType
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

/**
 * Manages external devices (LiDAR via USB Serial)
 */
class ExternalDeviceManager(
    private val usbSerialManager: UsbSerialManager,
    private val coroutineScope: CoroutineScope,
    private val onNotification: (String) -> Unit
) {

    private val _externalDevices = MutableStateFlow<List<ExternalDeviceInfo>>(emptyList())
    val externalDevices: StateFlow<List<ExternalDeviceInfo>> = _externalDevices

    private val _devicesBeingToggled = MutableStateFlow<Set<String>>(emptySet())
    val devicesBeingToggled: StateFlow<Set<String>> = _devicesBeingToggled

    fun updateLidarBaudrate(uniqueId: String, baudrate: Int) {
        _externalDevices.value = _externalDevices.value.map { device ->
            if (device.uniqueId == uniqueId && device.deviceType == ExternalDeviceType.LIDAR) {
                device.copy(baudrate = baudrate)
            } else {
                device
            }
        }
    }

    fun connectLidar(uniqueId: String) {
        coroutineScope.launch(Dispatchers.IO) {
            // Get current baudrate from state
            val currentDevice = _externalDevices.value.find { it.uniqueId == uniqueId }
            val baudrate = currentDevice?.baudrate ?: 512000

            // Find the USB Serial device from detected devices
            val device = usbSerialManager.detectLidarDevices().find {
                it.uniqueId == uniqueId
            } ?: run {
                withContext(Dispatchers.Main) {
                    onNotification("LIDAR device not found: $uniqueId")
                }
                return@launch
            }

            val devicePath = device.usbPath

            // Connect via USB Serial
            val success = NativeBridge.nativeConnectLidar(devicePath, uniqueId, baudrate)
            withContext(Dispatchers.Main) {
                if (success) {
                    refreshExternalDevices()
                } else {
                    onNotification("Failed to initialize LIDAR SDK")
                }
            }
        }
    }

    fun disconnectLidar(uniqueId: String) {
        if (_devicesBeingToggled.value.contains(uniqueId)) return

        coroutineScope.launch(Dispatchers.IO) {
            _devicesBeingToggled.value = _devicesBeingToggled.value + uniqueId

            val success = NativeBridge.nativeDisconnectLidar(uniqueId)

            withContext(Dispatchers.Main) {
                if (success) {
                    android.util.Log.i("ExternalDeviceManager", "LIDAR disconnected: $uniqueId")
                    refreshExternalDevices()
                } else {
                    onNotification("Failed to disconnect LIDAR")
                }

                _devicesBeingToggled.value = _devicesBeingToggled.value - uniqueId
            }
        }
    }

    fun enableLidar(uniqueId: String) {
        if (_devicesBeingToggled.value.contains(uniqueId)) return

        coroutineScope.launch(Dispatchers.IO) {
            _devicesBeingToggled.value = _devicesBeingToggled.value + uniqueId

            val success = NativeBridge.nativeEnableLidar(uniqueId)

            withContext(Dispatchers.Main) {
                if (success) {
                    android.util.Log.i("ExternalDeviceManager", "LIDAR publishing enabled: $uniqueId")
                    refreshExternalDevices()
                } else {
                    onNotification("Failed to enable LIDAR publishing")
                }

                _devicesBeingToggled.value = _devicesBeingToggled.value - uniqueId
            }
        }
    }

    fun disableLidar(uniqueId: String) {
        if (_devicesBeingToggled.value.contains(uniqueId)) return

        coroutineScope.launch(Dispatchers.IO) {
            _devicesBeingToggled.value = _devicesBeingToggled.value + uniqueId

            val success = NativeBridge.nativeDisableLidar(uniqueId)

            withContext(Dispatchers.Main) {
                if (success) {
                    android.util.Log.i("ExternalDeviceManager", "LIDAR publishing disabled: $uniqueId")
                    refreshExternalDevices()
                } else {
                    onNotification("Failed to disable LIDAR publishing")
                }

                _devicesBeingToggled.value = _devicesBeingToggled.value - uniqueId
            }
        }
    }

    fun isDeviceBeingToggled(uniqueId: String): Boolean {
        return _devicesBeingToggled.value.contains(uniqueId)
    }

    fun scanForExternalDevices() {
        coroutineScope.launch(Dispatchers.IO) {
            android.util.Log.i("ExternalDeviceManager", "Scanning for USB Serial LIDAR devices...")

            val usbDevices = usbSerialManager.detectLidarDevices()

            android.util.Log.i("ExternalDeviceManager", "Found ${usbDevices.size} USB LIDAR device(s)")

            if (usbDevices.isEmpty()) {
                withContext(Dispatchers.Main) {
                    onNotification("No LIDAR devices found. Ensure device is connected via USB.")
                }
            } else {
                withContext(Dispatchers.Main) {
                    onNotification("Found ${usbDevices.size} LIDAR device(s)")
                }
            }

            withContext(Dispatchers.Main) {
                refreshExternalDevices()
            }
        }
    }

    fun handleUsbDeviceAttached(device: UsbDevice, rosStarted: Boolean, onNavigateToExternalSensors: () -> Unit) {
        coroutineScope.launch(Dispatchers.IO) {
            android.util.Log.i("ExternalDeviceManager", "Handling USB device attachment: ${device.deviceName}")

            val detectedDevices = usbSerialManager.detectLidarDevices()
            val matchingDevice = detectedDevices.find {
                it.vendorId == device.vendorId && it.productId == device.productId
            }

            if (matchingDevice != null) {
                android.util.Log.i("ExternalDeviceManager", "USB device identified as LIDAR: ${matchingDevice.name}")

                withContext(Dispatchers.Main) {
                    if (rosStarted) {
                        onNavigateToExternalSensors()
                        refreshExternalDevices()
                    } else {
                        onNotification("LIDAR detected: ${matchingDevice.name}. Start ROS to use it.")
                    }
                }
            } else {
                android.util.Log.i("ExternalDeviceManager", "USB device is not a recognized LIDAR")
            }
        }
    }

    fun refreshExternalDevices() {
        val nativeDevices = try {
            NativeBridge.nativeGetLidarList().toList()
        } catch (e: Exception) {
            android.util.Log.e("ExternalDeviceManager", "Failed to get native LIDAR list", e)
            emptyList()
        }

        val usbDevices = usbSerialManager.detectLidarDevices()

        android.util.Log.d("ExternalDeviceManager", "${usbDevices.size} USB devices, ${nativeDevices.size} connected")

        val deviceMap = mutableMapOf<String, ExternalDeviceInfo>()

        // First add all USB-detected devices (disconnected state)
        usbDevices.forEach { usbDevice ->
            deviceMap[usbDevice.uniqueId] = usbDevice.copy(connected = false, enabled = false)
        }

        // Then merge native layer state (connected devices)
        nativeDevices.forEach { nativeDevice ->
            val usbDetectedDevice = deviceMap[nativeDevice.uniqueId]
            if (usbDetectedDevice != null) {
                deviceMap[nativeDevice.uniqueId] = usbDetectedDevice.copy(
                    connected = nativeDevice.connected,
                    enabled = nativeDevice.enabled,
                    topicName = nativeDevice.topicName,
                    topicType = nativeDevice.topicType
                )
            } else {
                deviceMap[nativeDevice.uniqueId] = nativeDevice
            }
        }

        _externalDevices.value = deviceMap.values.toList()
    }
}
