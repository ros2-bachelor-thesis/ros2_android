package com.github.mowerick.ros2.android.ui.screens

import android.graphics.Bitmap
import androidx.compose.foundation.Image
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.ArrowBack
import androidx.compose.material3.Button
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.FilterChip
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.unit.dp
import com.github.mowerick.ros2.android.viewmodel.managers.BeetlePredatorManager

@OptIn(ExperimentalMaterial3Api::class, ExperimentalLayoutApi::class)
@Composable
fun BeetlePredatorScreen(
    state: BeetlePredatorManager.BeetlePredatorState,
    debugFrame: Bitmap?,
    onBack: () -> Unit,
    onEnable: () -> Unit,
    onDisable: () -> Unit,
    onToggleLabel: (String) -> Unit
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
            // Start/Stop button
            if (state.enabled) {
                OutlinedButton(
                    onClick = onDisable,
                    modifier = Modifier.fillMaxWidth().height(48.dp)
                ) {
                    Text("Stop")
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
                            else -> "Start Detection"
                        }
                    )
                }
            }

            // Label filter chips
            Text(
                text = "Publish on detection of:",
                style = MaterialTheme.typography.labelLarge
            )
            FlowRow(
                horizontalArrangement = Arrangement.spacedBy(8.dp)
            ) {
                val labels = listOf(
                    "cpb_beetle" to "Beetle",
                    "cpb_larva" to "Larva",
                    "cpb_eggs" to "Eggs"
                )
                labels.forEach { (id, displayName) ->
                    FilterChip(
                        selected = id in state.labelFilter,
                        onClick = { onToggleLabel(id) },
                        label = { Text(displayName) }
                    )
                }
            }

            // Status info
            if (state.enabled) {
                Text(
                    text = "New detections published: ${state.newDetectionCount}",
                    style = MaterialTheme.typography.bodyMedium,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
            }

            // Camera preview with detection overlay
            if (debugFrame != null) {
                Image(
                    bitmap = debugFrame.asImageBitmap(),
                    contentDescription = "Detection preview",
                    modifier = Modifier
                        .fillMaxWidth()
                        .aspectRatio(debugFrame.width.toFloat() / debugFrame.height.toFloat()),
                    contentScale = ContentScale.Fit
                )
            } else if (state.enabled) {
                Text(
                    text = "Waiting for camera frame...",
                    style = MaterialTheme.typography.bodyMedium,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                    modifier = Modifier.padding(vertical = 32.dp)
                )
            }

            Spacer(modifier = Modifier.height(16.dp))
        }
    }
}
