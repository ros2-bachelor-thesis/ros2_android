package com.github.mowerick.ros2.android.viewmodel.managers

import android.content.Context
import android.net.wifi.WifiManager
import com.github.mowerick.ros2.android.util.NativeBridge
import com.github.mowerick.ros2.android.interfaces.NetworkInterfaceProvider
import com.github.mowerick.ros2.android.util.getDefaultDeviceId
import com.github.mowerick.ros2.android.util.sanitizeDeviceId
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow

/**
 * Manages ROS 2 lifecycle (startup, shutdown, network interfaces, multicast lock)
 */
class RosLifecycleManager(
    private val applicationContext: Context,
    private val networkProvider: NetworkInterfaceProvider
) {

    private val _rosStarted = MutableStateFlow(false)
    val rosStarted: StateFlow<Boolean> = _rosStarted

    private val _rosDomainId = MutableStateFlow(-1)
    val rosDomainId: StateFlow<Int> = _rosDomainId

    private val _deviceId = MutableStateFlow(getDefaultDeviceId(applicationContext))
    val deviceId: StateFlow<String> = _deviceId

    private val _networkInterfaces = MutableStateFlow<List<String>>(emptyList())
    val networkInterfaces: StateFlow<List<String>> = _networkInterfaces

    private val _selectedNetworkInterface = MutableStateFlow<String?>(null)
    val selectedNetworkInterface: StateFlow<String?> = _selectedNetworkInterface

    private var multicastLock: WifiManager.MulticastLock? = null

    fun setDomainId(id: Int) {
        _rosDomainId.value = id
    }

    fun setDeviceId(id: String) {
        _deviceId.value = id
    }

    fun loadNetworkInterfaces() {
        try {
            val ifaces = NativeBridge.nativeGetNetworkInterfaces()
            _networkInterfaces.value = ifaces.toList()
        } catch (e: Exception) {
            android.util.Log.e("RosLifecycleManager", "Failed to load network interfaces", e)
        }
    }

    fun refreshNetworkInterfaces() {
        try {
            val ifaces = networkProvider.queryNetworkInterfaces()
            NativeBridge.nativeSetNetworkInterfaces(ifaces)
            loadNetworkInterfaces()
        } catch (e: Exception) {
            android.util.Log.e("RosLifecycleManager", "Failed to refresh network interfaces", e)
        }
    }

    fun startRos(domainId: Int, networkInterface: String, deviceId: String) {
        acquireMulticastLock()

        val sanitizedDeviceId = sanitizeDeviceId(deviceId)
        _deviceId.value = sanitizedDeviceId
        NativeBridge.nativeStartRos(domainId, networkInterface, sanitizedDeviceId)
        _rosDomainId.value = domainId
        _selectedNetworkInterface.value = networkInterface
        _rosStarted.value = true
    }

    fun releaseMulticastLock() {
        try {
            multicastLock?.let {
                if (it.isHeld) {
                    it.release()
                }
            }
            multicastLock = null
        } catch (e: Exception) {
            android.util.Log.e("RosLifecycleManager", "Failed to release multicast lock", e)
        }
    }

    private fun acquireMulticastLock() {
        try {
            val wifiManager = applicationContext.getSystemService(Context.WIFI_SERVICE) as WifiManager
            multicastLock = wifiManager.createMulticastLock("ros2_dds_discovery").apply {
                setReferenceCounted(false)
                acquire()
            }
        } catch (e: Exception) {
            android.util.Log.e("RosLifecycleManager", "Failed to acquire multicast lock", e)
        }
    }
}
