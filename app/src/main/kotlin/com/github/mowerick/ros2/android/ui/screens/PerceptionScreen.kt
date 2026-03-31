package com.github.mowerick.ros2.android.ui.screens

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.ArrowBack
import androidx.compose.material3.Button
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import com.github.mowerick.ros2.android.ui.components.CollapsibleCard
import com.github.mowerick.ros2.android.viewmodel.RosViewModel

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun PerceptionScreen(
    perceptionState: RosViewModel.PerceptionState,
    onBack: () -> Unit,
    onEnable: () -> Unit,
    onDisable: () -> Unit
) {
    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text("Object Detection") },
                navigationIcon = {
                    IconButton(onClick = onBack) {
                        Icon(Icons.Filled.ArrowBack, contentDescription = "Back")
                    }
                }
            )
        }
    ) { padding ->
        LazyColumn(
            modifier = Modifier
                .fillMaxSize()
                .padding(padding)
                .padding(horizontal = 16.dp, vertical = 8.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            // Enable/Disable controls
            item {
                Column(
                    modifier = Modifier.fillMaxWidth(),
                    verticalArrangement = Arrangement.spacedBy(8.dp)
                ) {
                    if (!perceptionState.enabled) {
                        Button(
                            onClick = onEnable,
                            modifier = Modifier.fillMaxWidth(),
                            enabled = perceptionState.modelsLoaded
                        ) {
                            Text(if (perceptionState.modelsLoaded) "Enable Detection" else "Loading Models...")
                        }
                    } else {
                        OutlinedButton(
                            onClick = onDisable,
                            modifier = Modifier.fillMaxWidth()
                        ) {
                            Text("Disable Detection")
                        }
                    }
                }
            }

            // Pipeline info
            item {
                CollapsibleCard(
                    title = "Pipeline Info",
                    initiallyExpanded = true
                ) {
                    Text(
                        text = "Detector: YOLOv9-s (NCNN)",
                        style = MaterialTheme.typography.bodyMedium
                    )
                    Text(
                        text = "Tracker: Deep SORT (MARS ReID)",
                        style = MaterialTheme.typography.bodyMedium,
                        modifier = Modifier.padding(top = 4.dp)
                    )
                    Text(
                        text = "Classes: CPB Beetle, Larva, Eggs",
                        style = MaterialTheme.typography.bodyMedium,
                        modifier = Modifier.padding(top = 4.dp)
                    )
                    Text(
                        text = "Input: ZED 2i (RGB + Depth + Point Cloud)",
                        style = MaterialTheme.typography.bodyMedium,
                        modifier = Modifier.padding(top = 4.dp)
                    )
                }
            }

            // Statistics
            item {
                CollapsibleCard(
                    title = "Statistics",
                    initiallyExpanded = true
                ) {
                    Text(
                        text = "Status: ${if (perceptionState.enabled) "Running" else "Stopped"}",
                        style = MaterialTheme.typography.bodyMedium
                    )
                    Text(
                        text = "Total Detections: ${perceptionState.totalDetections}",
                        style = MaterialTheme.typography.bodyMedium,
                        modifier = Modifier.padding(top = 4.dp)
                    )
                    Text(
                        text = "Active Tracks: ${perceptionState.activeTrackCount}",
                        style = MaterialTheme.typography.bodyMedium,
                        modifier = Modifier.padding(top = 4.dp)
                    )
                    Text(
                        text = "Queue Size: ${perceptionState.queueSize}",
                        style = MaterialTheme.typography.bodyMedium,
                        modifier = Modifier.padding(top = 4.dp)
                    )
                }
            }

            // Published topics
            item {
                CollapsibleCard(
                    title = "Published Topics",
                    initiallyExpanded = false
                ) {
                    Text(
                        text = "Centers (Point):",
                        style = MaterialTheme.typography.titleSmall,
                        modifier = Modifier.padding(bottom = 4.dp)
                    )
                    Text(
                        text = "/{device_id}/cpb_beetle_center",
                        style = MaterialTheme.typography.bodySmall,
                        modifier = Modifier.padding(start = 8.dp)
                    )
                    Text(
                        text = "/{device_id}/cpb_larva_center",
                        style = MaterialTheme.typography.bodySmall,
                        modifier = Modifier.padding(start = 8.dp)
                    )
                    Text(
                        text = "/{device_id}/cpb_eggs_center",
                        style = MaterialTheme.typography.bodySmall,
                        modifier = Modifier.padding(start = 8.dp)
                    )

                    Text(
                        text = "Cropped Clouds (PointCloud2):",
                        style = MaterialTheme.typography.titleSmall,
                        modifier = Modifier.padding(top = 12.dp, bottom = 4.dp)
                    )
                    Text(
                        text = "/{device_id}/cpb_beetle",
                        style = MaterialTheme.typography.bodySmall,
                        modifier = Modifier.padding(start = 8.dp)
                    )
                    Text(
                        text = "/{device_id}/cpb_larva",
                        style = MaterialTheme.typography.bodySmall,
                        modifier = Modifier.padding(start = 8.dp)
                    )
                    Text(
                        text = "/{device_id}/cpb_eggs",
                        style = MaterialTheme.typography.bodySmall,
                        modifier = Modifier.padding(start = 8.dp)
                    )
                }
            }

            // Subscribed topics
            item {
                CollapsibleCard(
                    title = "Subscribed Topics",
                    initiallyExpanded = false
                ) {
                    Text(
                        text = "/zed/zed_node/rgb/image_rect_color/compressed",
                        style = MaterialTheme.typography.bodySmall
                    )
                    Text(
                        text = "/zed/zed_node/depth/depth_registered",
                        style = MaterialTheme.typography.bodySmall,
                        modifier = Modifier.padding(top = 4.dp)
                    )
                    Text(
                        text = "/zed/zed_node/point_cloud/cloud_registered",
                        style = MaterialTheme.typography.bodySmall,
                        modifier = Modifier.padding(top = 4.dp)
                    )
                }
            }
        }
    }
}
