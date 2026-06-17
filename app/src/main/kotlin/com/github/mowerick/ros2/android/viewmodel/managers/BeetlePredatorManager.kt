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
import org.json.JSONObject

class BeetlePredatorManager(
    private val applicationContext: Context,
    private val coroutineScope: CoroutineScope,
    private val gpsManager: GpsManager,
    private val getLocationSettingsLauncher: () -> ActivityResultLauncher<IntentSenderRequest>?,
    private val onError: (String) -> Unit
) {

    data class SnapshotDetection(
        val label: String,
        val classId: Int,
        val confidence: Float,
        val bboxX: Int,
        val bboxY: Int,
        val bboxW: Int,
        val bboxH: Int
    )

    data class GpsSnapshot(
        val lat: Double,
        val lon: Double,
        val alt: Double,
        val accuracy: Float,
        val hasGps: Boolean
    )

    data class BeetlePredatorState(
        val enabled: Boolean = false,
        val modelsLoaded: Boolean = false,
        val visualizationEnabled: Boolean = false,
        val labelFilter: Set<String> = setOf(),
        val newDetectionCount: Int = 0,
        val isProcessingSnapshot: Boolean = false,
        val hasSnapshotResult: Boolean = false,
        val snapshotDetections: List<SnapshotDetection> = emptyList(),
        val snapshotGps: GpsSnapshot? = null
    )

    private val _state = MutableStateFlow(BeetlePredatorState())
    val state: StateFlow<BeetlePredatorState> = _state

    // Live camera viewfinder (10 Hz raw frames while enabled)
    private val _previewFrame = MutableStateFlow<Bitmap?>(null)
    val previewFrame: StateFlow<Bitmap?> = _previewFrame

    // Annotated detection result (set after TakeSnapshot completes)
    private val _snapshotFrame = MutableStateFlow<Bitmap?>(null)
    val snapshotFrame: StateFlow<Bitmap?> = _snapshotFrame

    // Legacy alias so existing ViewModel wiring still compiles
    val debugFrame: StateFlow<Bitmap?> = _previewFrame

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
        if (!gpsManager.isRunning()) {
            Log.i(TAG, "Starting GPS for Beetle Predator")
            val launcher = getLocationSettingsLauncher()
            gpsManager.startWithChecks(launcher)
        }

        coroutineScope.launch(Dispatchers.IO) {
            try {
                val modelsPath = "${applicationContext.filesDir.absolutePath}/models"
                NativeBridge.enableBeetlePredator(modelsPath)

                val mask = labelSetToMask(_state.value.labelFilter)
                NativeBridge.setBeetlePredatorLabelFilter(mask)
                NativeBridge.enableBeetlePredatorVisualization(true)

                withContext(Dispatchers.Main) {
                    _state.value = _state.value.copy(
                        enabled = true,
                        modelsLoaded = true,
                        visualizationEnabled = true,
                        hasSnapshotResult = false,
                        isProcessingSnapshot = false,
                        snapshotDetections = emptyList(),
                        snapshotGps = null
                    )
                    _snapshotFrame.value = null
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

                Log.i(TAG, "Stopping GPS for Beetle Predator")
                gpsManager.stop()

                withContext(Dispatchers.Main) {
                    _state.value = _state.value.copy(
                        enabled = false,
                        visualizationEnabled = false,
                        isProcessingSnapshot = false,
                        hasSnapshotResult = false,
                        snapshotDetections = emptyList(),
                        snapshotGps = null
                    )
                    _previewFrame.value = null
                    _snapshotFrame.value = null
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
        NativeBridge.setBeetlePredatorLabelFilter(labelSetToMask(current))
    }

    fun takeSnapshot() {
        if (!_state.value.enabled || _state.value.isProcessingSnapshot) return

        _state.value = _state.value.copy(
            isProcessingSnapshot = true,
            hasSnapshotResult = false
        )

        coroutineScope.launch(Dispatchers.IO) {
            try {
                // Blocks ~300-900 ms on the IO thread while detection runs
                NativeBridge.takeBeetlePredatorSnapshot()
                // Result arrives via onDebugFrameUpdate("beetle_predator_snapshot") callback
            } catch (e: Exception) {
                Log.e(TAG, "Failed to take snapshot", e)
                withContext(Dispatchers.Main) {
                    _state.value = _state.value.copy(isProcessingSnapshot = false)
                    onError("Snapshot failed: ${e.message}")
                }
            }
        }
    }

    fun retake() {
        _state.value = _state.value.copy(
            hasSnapshotResult = false,
            isProcessingSnapshot = false,
            snapshotDetections = emptyList(),
            snapshotGps = null
        )
        _snapshotFrame.value = null
    }

    // Called by ViewModel when "beetle_predator_rgb" debug frame callback fires
    fun updatePreviewFrame() {
        coroutineScope.launch(Dispatchers.IO) {
            try {
                val bitmap = NativeBridge.getBeetlePredatorDebugFrame()
                withContext(Dispatchers.Main) {
                    _previewFrame.value = bitmap
                }
            } catch (e: Exception) {
                Log.e(TAG, "Failed to get preview frame", e)
            }
        }
    }

    // Called by ViewModel when "beetle_predator_snapshot" debug frame callback fires
    fun updateSnapshotResult() {
        coroutineScope.launch(Dispatchers.IO) {
            try {
                val bitmap = NativeBridge.getBeetlePredatorSnapshotFrame()
                val json = NativeBridge.getBeetlePredatorLastDetections()
                val count = NativeBridge.getBeetlePredatorDetectionCount()

                val detections = parseDetectionsJson(json)
                val gps = parseGpsJson(json)

                withContext(Dispatchers.Main) {
                    _snapshotFrame.value = bitmap
                    _state.value = _state.value.copy(
                        isProcessingSnapshot = false,
                        hasSnapshotResult = true,
                        snapshotDetections = detections,
                        snapshotGps = gps,
                        newDetectionCount = count
                    )
                }
            } catch (e: Exception) {
                Log.e(TAG, "Failed to get snapshot result", e)
                withContext(Dispatchers.Main) {
                    _state.value = _state.value.copy(isProcessingSnapshot = false)
                }
            }
        }
    }

    // Legacy: kept so existing ViewModel debug frame callback compiles during transition
    fun updateDebugFrame() = updatePreviewFrame()

    private fun parseDetectionsJson(json: String): List<SnapshotDetection> {
        if (json == "{}") return emptyList()
        return try {
            val obj = JSONObject(json)
            val arr = obj.optJSONArray("detections") ?: return emptyList()
            (0 until arr.length()).map { i ->
                val d = arr.getJSONObject(i)
                SnapshotDetection(
                    label = d.getString("label"),
                    classId = d.getInt("class_id"),
                    confidence = d.getDouble("confidence").toFloat(),
                    bboxX = d.getInt("bbox_x"),
                    bboxY = d.getInt("bbox_y"),
                    bboxW = d.getInt("bbox_w"),
                    bboxH = d.getInt("bbox_h")
                )
            }
        } catch (e: Exception) {
            Log.e(TAG, "Failed to parse detections JSON", e)
            emptyList()
        }
    }

    private fun parseGpsJson(json: String): GpsSnapshot? {
        if (json == "{}") return null
        return try {
            val obj = JSONObject(json)
            GpsSnapshot(
                lat = obj.getDouble("latitude"),
                lon = obj.getDouble("longitude"),
                alt = obj.getDouble("altitude"),
                accuracy = obj.getDouble("accuracy").toFloat(),
                hasGps = obj.optBoolean("has_gps", false)
            )
        } catch (e: Exception) {
            Log.e(TAG, "Failed to parse GPS JSON", e)
            null
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
