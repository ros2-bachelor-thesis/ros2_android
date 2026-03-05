package com.github.sloretz.sensors_for_ros

import android.Manifest
import android.content.pm.PackageManager
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.core.content.ContextCompat
import androidx.lifecycle.viewmodel.compose.viewModel
import com.github.sloretz.sensors_for_ros.ui.screens.CameraDetailScreen
import com.github.sloretz.sensors_for_ros.ui.screens.DomainIdScreen
import com.github.sloretz.sensors_for_ros.ui.screens.SensorDetailScreen
import com.github.sloretz.sensors_for_ros.ui.screens.SensorListScreen
import com.github.sloretz.sensors_for_ros.ui.theme.SensorsForRosTheme
import com.github.sloretz.sensors_for_ros.viewmodel.RosViewModel
import com.github.sloretz.sensors_for_ros.viewmodel.Screen
import java.net.NetworkInterface

class MainActivity : ComponentActivity() {

    private val requestCameraPermission =
        registerForActivityResult(ActivityResultContracts.RequestPermission()) { granted ->
            NativeBridge.nativeOnPermissionResult("CAMERA", granted)
        }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        NativeBridge.nativeInit(cacheDir.absolutePath, packageName)
        NativeBridge.nativeSetNetworkInterfaces(queryNetworkInterfaces())

        if (ContextCompat.checkSelfPermission(this, Manifest.permission.CAMERA)
            != PackageManager.PERMISSION_GRANTED
        ) {
            requestCameraPermission.launch(Manifest.permission.CAMERA)
        } else {
            NativeBridge.nativeOnPermissionResult("CAMERA", true)
        }

        setContent {
            SensorsForRosTheme {
                val vm: RosViewModel = viewModel()
                LaunchedEffect(Unit) { vm.loadNetworkInterfaces() }

                val screen by vm.screen.collectAsState()
                val sensors by vm.sensors.collectAsState()
                val cameras by vm.cameras.collectAsState()
                val reading by vm.currentReading.collectAsState()
                val networkInterfaces by vm.networkInterfaces.collectAsState()

                when (val s = screen) {
                    is Screen.DomainId -> DomainIdScreen(
                        networkInterfaces = networkInterfaces,
                        onStartRos = { domainId, iface -> vm.startRos(domainId, iface) }
                    )
                    is Screen.SensorList -> SensorListScreen(
                        sensors = sensors,
                        cameras = cameras,
                        onSensorClick = { vm.navigateToSensor(it) },
                        onCameraClick = { vm.navigateToCamera(it) }
                    )
                    is Screen.SensorDetail -> SensorDetailScreen(
                        sensor = s.sensor,
                        reading = reading,
                        onBack = { vm.navigateBack() }
                    )
                    is Screen.CameraDetail -> CameraDetailScreen(
                        camera = s.camera,
                        onBack = { vm.navigateBack() },
                        onEnable = { vm.enableCamera(s.camera.uniqueId) },
                        onDisable = { vm.disableCamera(s.camera.uniqueId) }
                    )
                }
            }
        }
    }

    override fun onDestroy() {
        NativeBridge.nativeDestroy()
        super.onDestroy()
    }

    private fun queryNetworkInterfaces(): Array<String> {
        return try {
            NetworkInterface.getNetworkInterfaces()
                ?.toList()
                ?.map { it.name }
                ?.toTypedArray()
                ?: emptyArray()
        } catch (e: Exception) {
            emptyArray()
        }
    }
}
