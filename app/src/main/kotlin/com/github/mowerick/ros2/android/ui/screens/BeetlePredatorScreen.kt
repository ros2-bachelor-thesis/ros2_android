package com.github.mowerick.ros2.android.ui.screens

import android.graphics.Bitmap
import androidx.compose.foundation.Image
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.ArrowBack
import androidx.compose.material3.Badge
import androidx.compose.material3.Button
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.FilterChip
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Scaffold
import androidx.compose.material3.SuggestionChip
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.unit.dp
import com.github.mowerick.ros2.android.viewmodel.managers.BeetlePredatorManager

@OptIn(ExperimentalMaterial3Api::class, ExperimentalLayoutApi::class)
@Composable
fun BeetlePredatorScreen(
    state: BeetlePredatorManager.BeetlePredatorState,
    previewFrame: Bitmap?,
    snapshotFrame: Bitmap?,
    onBack: () -> Unit,
    onEnable: () -> Unit,
    onDisable: () -> Unit,
    onToggleLabel: (String) -> Unit,
    onTakeSnapshot: () -> Unit,
    onRetake: () -> Unit
) {
    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text("Beetle Predator") },
                navigationIcon = {
                    IconButton(onClick = onBack) {
                        Icon(Icons.Filled.ArrowBack, contentDescription = "Back")
                    }
                }
            )
        }
    ) { padding ->
        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(padding)
                .padding(horizontal = 16.dp)
                .verticalScroll(rememberScrollState()),
            verticalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            // Label filter chips (disabled while running or processing)
            Text(
                text = "Detect:",
                style = MaterialTheme.typography.labelLarge
            )
            FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                val labels = listOf(
                    "cpb_beetle" to "Beetle",
                    "cpb_larva" to "Larva",
                    "cpb_eggs" to "Eggs"
                )
                labels.forEach { (id, displayName) ->
                    FilterChip(
                        selected = id in state.labelFilter,
                        onClick = { onToggleLabel(id) },
                        enabled = !state.enabled,
                        label = { Text(displayName) }
                    )
                }
            }

            // Camera enable / disable
            if (state.enabled) {
                OutlinedButton(
                    onClick = onDisable,
                    modifier = Modifier.fillMaxWidth().height(48.dp)
                ) {
                    Text("Disable Camera")
                }
            } else {
                Button(
                    onClick = onEnable,
                    enabled = state.modelsLoaded && state.labelFilter.isNotEmpty(),
                    modifier = Modifier.fillMaxWidth().height(48.dp)
                ) {
                    Text(
                        when {
                            !state.modelsLoaded -> "Models not loaded"
                            state.labelFilter.isEmpty() -> "Select at least one label"
                            else -> "Enable Camera"
                        }
                    )
                }
            }

            if (state.hasSnapshotResult) {
                // --- Result view ---
                snapshotFrame?.let { bmp ->
                    Image(
                        bitmap = bmp.asImageBitmap(),
                        contentDescription = "Detection result",
                        modifier = Modifier
                            .fillMaxWidth()
                            .aspectRatio(bmp.width.toFloat() / bmp.height.toFloat()),
                        contentScale = ContentScale.Fit
                    )
                }

                // GPS row
                val gps = state.snapshotGps
                if (gps != null && gps.hasGps) {
                    Text(
                        text = "Lat: %.6f  Lon: %.6f  Alt: %.1f m  ±%.1f m".format(
                            gps.lat, gps.lon, gps.alt, gps.accuracy
                        ),
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                } else {
                    Text(
                        text = "GPS unavailable",
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.error
                    )
                }

                // Detection list
                if (state.snapshotDetections.isEmpty()) {
                    Text(
                        text = "No detections in this frame",
                        style = MaterialTheme.typography.bodyMedium,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                } else {
                    Text(
                        text = "${state.snapshotDetections.size} detection(s):",
                        style = MaterialTheme.typography.labelLarge
                    )
                    state.snapshotDetections.forEach { det ->
                        Row(
                            modifier = Modifier.fillMaxWidth(),
                            horizontalArrangement = Arrangement.spacedBy(8.dp),
                            verticalAlignment = Alignment.CenterVertically
                        ) {
                            SuggestionChip(
                                onClick = {},
                                label = { Text(det.label) }
                            )
                            Text(
                                text = "${"%.0f".format(det.confidence * 100)}%",
                                style = MaterialTheme.typography.bodyMedium
                            )
                            Text(
                                text = "(${det.bboxX}, ${det.bboxY}  ${det.bboxW}×${det.bboxH})",
                                style = MaterialTheme.typography.bodySmall,
                                color = MaterialTheme.colorScheme.onSurfaceVariant
                            )
                        }
                    }
                }

                // Take Another button
                Button(
                    onClick = onRetake,
                    modifier = Modifier.fillMaxWidth().height(48.dp)
                ) {
                    Text("Take Another Photo")
                }

            } else {
                // --- Viewfinder view ---
                if (state.enabled) {
                    if (state.isProcessingSnapshot) {
                        Box(
                            modifier = Modifier.fillMaxWidth().height(240.dp),
                            contentAlignment = Alignment.Center
                        ) {
                            CircularProgressIndicator()
                        }
                    } else {
                        previewFrame?.let { bmp ->
                            Image(
                                bitmap = bmp.asImageBitmap(),
                                contentDescription = "Camera preview",
                                modifier = Modifier
                                    .fillMaxWidth()
                                    .aspectRatio(bmp.width.toFloat() / bmp.height.toFloat()),
                                contentScale = ContentScale.Fit
                            )
                        } ?: Text(
                            text = "Waiting for camera frame...",
                            style = MaterialTheme.typography.bodyMedium,
                            color = MaterialTheme.colorScheme.onSurfaceVariant,
                            modifier = Modifier.padding(vertical = 32.dp)
                        )

                        // Take Photo button
                        Button(
                            onClick = onTakeSnapshot,
                            enabled = !state.isProcessingSnapshot,
                            modifier = Modifier.fillMaxWidth().height(56.dp)
                        ) {
                            Text("Take Photo")
                        }
                    }
                }
            }

            Spacer(modifier = Modifier.height(16.dp))
        }
    }
}
