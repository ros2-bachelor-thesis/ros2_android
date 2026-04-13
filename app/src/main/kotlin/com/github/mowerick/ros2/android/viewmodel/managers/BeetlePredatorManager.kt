package com.github.mowerick.ros2.android.viewmodel.managers

import android.content.Context
import android.graphics.Bitmap
import android.util.Log
import androidx.activity.result.ActivityResultLauncher
import androidx.activity.result.IntentSenderRequest
import com.github.mowerick.ros2.android.util.NativeBridge
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

class BeetlePredatorManager(
    private val applicationContext: Context,
    private val coroutineScope: CoroutineScope,
    private val gpsManager: GpsManager,
    private val getLocationSettingsLauncher: () -> ActivityResultLauncher<IntentSenderRequest>?,
    private val onError: (String) -> Unit
) {

    data class BeetlePredatorState(
        val enabled: Boolean = false,
        val modelsLoaded: Boolean = false,
        val visualizationEnabled: Boolean = false,
        val labelFilter: Set<String> = setOf(),
        val newDetectionCount: Int = 0
    )

    private val _state = MutableStateFlow(BeetlePredatorState())
    val state: StateFlow<BeetlePredatorState> = _state

    private val _debugFrame = MutableStateFlow<Bitmap?>(null)
    val debugFrame: StateFlow<Bitmap?> = _debugFrame

    init {
        coroutineScope.launch(Dispatchers.IO) {
            val modelsDir = applicationContext.filesDir.resolve("models")
            val requiredFiles = listOf(
                "yolov9_s_pobed.ncnn.param",
                "yolov9_s_pobed.ncnn.bin",
                "osnet_ain_x1_0.ncnn.param",
                "osnet_ain_x1_0.ncnn.bin"
            )
            val allExist = requiredFiles.all { modelsDir.resolve(it).exists() }
            withContext(Dispatchers.Main) {
                _state.value = _state.value.copy(modelsLoaded = allExist)
            }
        }
    }

    fun enable() {
        // Ensure GPS is running (same check as built-in GPS sensor)
        if (!gpsManager.isRunning()) {
            Log.i(TAG, "Starting GPS for Beetle Predator")
            val launcher = getLocationSettingsLauncher()
            gpsManager.startWithChecks(launcher)
        }

        coroutineScope.launch(Dispatchers.IO) {
            try {
                val modelsPath = "${applicationContext.filesDir.absolutePath}/models"
                NativeBridge.enableBeetlePredator(modelsPath)

                // Apply current label filter to the newly created controller
                val mask = labelSetToMask(_state.value.labelFilter)
                NativeBridge.setBeetlePredatorLabelFilter(mask)

                // Enable visualization automatically
                NativeBridge.enableBeetlePredatorVisualization(true)

                withContext(Dispatchers.Main) {
                    _state.value = _state.value.copy(
                        enabled = true,
                        modelsLoaded = true,
                        visualizationEnabled = true
                    )
                }
            } catch (e: Exception) {
                Log.e(TAG, "Failed to enable Beetle Predator", e)
                withContext(Dispatchers.Main) {
                    onError("Failed to enable Beetle Predator: ${e.message}")
                }
            }
        }
    }

    fun disable() {
        coroutineScope.launch(Dispatchers.IO) {
            try {
                NativeBridge.disableBeetlePredator()

                // Stop GPS when beetle predator is disabled
                Log.i(TAG, "Stopping GPS for Beetle Predator")
                gpsManager.stop()

                withContext(Dispatchers.Main) {
                    _state.value = _state.value.copy(
                        enabled = false,
                        visualizationEnabled = false
                    )
                    _debugFrame.value = null
                }
            } catch (e: Exception) {
                Log.e(TAG, "Failed to disable Beetle Predator", e)
                withContext(Dispatchers.Main) {
                    onError("Failed to disable Beetle Predator: ${e.message}")
                }
            }
        }
    }

    fun toggleLabel(label: String) {
        val current = _state.value.labelFilter.toMutableSet()
        if (current.contains(label)) {
            current.remove(label)
        } else {
            current.add(label)
        }
        _state.value = _state.value.copy(labelFilter = current)

        // Convert to bitmask and send to native
        val mask = labelSetToMask(current)
        NativeBridge.setBeetlePredatorLabelFilter(mask)
    }

    fun updateDebugFrame() {
        coroutineScope.launch(Dispatchers.IO) {
            try {
                val bitmap = NativeBridge.getBeetlePredatorDebugFrame()
                val count = NativeBridge.getBeetlePredatorDetectionCount()

                withContext(Dispatchers.Main) {
                    _debugFrame.value = bitmap
                    _state.value = _state.value.copy(newDetectionCount = count)
                }
            } catch (e: Exception) {
                Log.e(TAG, "Failed to get debug frame", e)
            }
        }
    }

    private fun labelSetToMask(labels: Set<String>): Int {
        var mask = 0
        if ("cpb_beetle" in labels) mask = mask or 0x01
        if ("cpb_larva" in labels) mask = mask or 0x02
        if ("cpb_eggs" in labels) mask = mask or 0x04
        return mask
    }

    companion object {
        private const val TAG = "BeetlePredatorManager"
    }
}
