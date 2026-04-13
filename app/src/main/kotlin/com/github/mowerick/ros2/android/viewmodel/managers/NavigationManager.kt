package com.github.mowerick.ros2.android.viewmodel.managers

import com.github.mowerick.ros2.android.model.CameraInfo
import com.github.mowerick.ros2.android.model.ExternalDeviceInfo
import com.github.mowerick.ros2.android.model.PipelineNode
import com.github.mowerick.ros2.android.model.SensorInfo
import com.github.mowerick.ros2.android.viewmodel.Screen
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow

/**
 * Manages navigation state and screen transitions
 */
class NavigationManager(initialScreen: Screen = Screen.Dashboard) {

    private val _screen = MutableStateFlow<Screen>(initialScreen)
    val screen: StateFlow<Screen> = _screen

    private val navigationStack = mutableListOf<Screen>()

    init {
        if (initialScreen != Screen.Dashboard) {
            navigationStack.add(Screen.Dashboard)
        }
        navigationStack.add(initialScreen)
    }

    fun navigateToRosSetup() {
        navigateTo(Screen.RosSetup)
    }

    fun navigateToBuiltInSensors() {
        navigateTo(Screen.BuiltInSensors)
    }

    fun navigateToExternalSensors() {
        navigateTo(Screen.ExternalSensors)
    }

    fun navigateToSubsystem() {
        navigateTo(Screen.Subsystem)
    }

    fun navigateToSensor(sensor: SensorInfo) {
        navigateTo(Screen.SensorDetail(sensor.uniqueId))
    }

    fun navigateToCamera(camera: CameraInfo) {
        navigateTo(Screen.CameraDetail(camera.uniqueId))
    }

    fun navigateToLidar(device: ExternalDeviceInfo) {
        navigateTo(Screen.LidarDetail(device.uniqueId))
    }

    fun navigateToNode(node: PipelineNode) {
        navigateTo(Screen.NodeDetail(node.id))
    }

    fun navigateToCommandBridge() {
        navigateTo(Screen.CommandBridge)
    }

    fun navigateToDebugFullscreen() {
        navigateTo(Screen.DebugVisualizationFullscreen)
    }

    fun navigateToBeetlePredator() {
        navigateTo(Screen.BeetlePredator)
    }

    fun navigateBack() {
        if (navigationStack.size > 1) {
            navigationStack.removeLast()
            _screen.value = navigationStack.last()
        }
    }

    private fun navigateTo(screen: Screen) {
        navigationStack.add(screen)
        _screen.value = screen
    }
}
