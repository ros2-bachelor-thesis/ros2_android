package com.github.mowerick.ros2.android.viewmodel.managers

import android.content.Context
import com.github.mowerick.ros2.android.util.NativeBridge
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

/**
 * Manages object detection (perception) lifecycle and statistics
 */
class PerceptionManager(
    private val applicationContext: Context,
    private val coroutineScope: CoroutineScope,
    private val onError: (String) -> Unit
) {

    data class PerceptionState(
        val enabled: Boolean = false,
        val totalDetections: Int = 0,
        val activeTrackCount: Int = 0,
        val queueSize: Int = 0,
        val modelsLoaded: Boolean = false
    )

    private val _perceptionState = MutableStateFlow(PerceptionState())
    val perceptionState: StateFlow<PerceptionState> = _perceptionState

    init {
        // Check if perception models exist
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
                _perceptionState.value = _perceptionState.value.copy(modelsLoaded = allExist)
            }
        }
    }

    fun enable() {
        coroutineScope.launch(Dispatchers.IO) {
            try {
                val modelsPath = "${applicationContext.filesDir.absolutePath}/models"
                NativeBridge.enablePerception(modelsPath)

                withContext(Dispatchers.Main) {
                    _perceptionState.value = _perceptionState.value.copy(
                        enabled = true,
                        modelsLoaded = true
                    )
                }
            } catch (e: Exception) {
                android.util.Log.e("PerceptionManager", "Failed to enable perception", e)
                withContext(Dispatchers.Main) {
                    onError("Failed to enable object detection: ${e.message}")
                }
            }
        }
    }

    fun disable() {
        coroutineScope.launch(Dispatchers.IO) {
            try {
                NativeBridge.disablePerception()

                withContext(Dispatchers.Main) {
                    _perceptionState.value = _perceptionState.value.copy(enabled = false)
                }
            } catch (e: Exception) {
                android.util.Log.e("PerceptionManager", "Failed to disable perception", e)
                withContext(Dispatchers.Main) {
                    onError("Failed to disable object detection: ${e.message}")
                }
            }
        }
    }

    fun setEnabled(enabled: Boolean) {
        _perceptionState.value = _perceptionState.value.copy(enabled = enabled)
    }

    fun updateStats(totalDetections: Int, activeTrackCount: Int, queueSize: Int) {
        _perceptionState.value = _perceptionState.value.copy(
            totalDetections = totalDetections,
            activeTrackCount = activeTrackCount,
            queueSize = queueSize
        )
    }
}
