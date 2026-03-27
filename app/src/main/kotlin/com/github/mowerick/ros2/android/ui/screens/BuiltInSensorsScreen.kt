package com.github.mowerick.ros2.android.ui.screens

import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.ArrowBack
import androidx.compose.material.icons.filled.ChevronRight
import androidx.compose.material3.Divider
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.ListItem
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import com.github.mowerick.ros2.android.model.CameraInfo
import com.github.mowerick.ros2.android.model.SensorInfo

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun BuiltInSensorsScreen(
    sensors: List<SensorInfo>,
    cameras: List<CameraInfo>,
    onBack: () -> Unit,
    onSensorClick: (SensorInfo) -> Unit,
    onCameraClick: (CameraInfo) -> Unit,
    onSensorToggle: (SensorInfo, Boolean) -> Unit,
    onCameraToggle: (CameraInfo, Boolean) -> Unit
) {
    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text("Built-in Sensors") },
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
        ) {
            if (sensors.isNotEmpty()) {
                item {
                    Text(
                        text = "Sensors",
                        style = MaterialTheme.typography.titleSmall,
                        modifier = Modifier.padding(start = 16.dp, top = 8.dp, bottom = 4.dp)
                    )
                }
                items(sensors, key = { it.uniqueId }) { sensor ->
                    ListItem(
                        headlineContent = { Text(sensor.prettyName) },
                        supportingContent = {
                            Column {
                                Text(
                                    text = "Topic: ${sensor.topicName}",
                                    maxLines = 1,
                                    overflow = TextOverflow.Ellipsis
                                )
                            }
                        },
                        trailingContent = {
                            Row(
                                verticalAlignment = Alignment.CenterVertically // <-- Add this right here
                            ) {
                                Switch(
                                    checked = sensor.enabled,
                                    onCheckedChange = { onSensorToggle(sensor, it) }
                                )
                                Spacer(modifier = Modifier.width(8.dp))
                                Icon(
                                    Icons.Filled.ChevronRight,
                                    contentDescription = "View details",
                                    tint = MaterialTheme.colorScheme.onSurfaceVariant
                                )
                            }
                        },
                        modifier = Modifier.clickable { onSensorClick(sensor) }
                    )
                    Divider()
                }
            }
            if (cameras.isNotEmpty()) {
                item {
                    Text(
                        text = "Cameras",
                        style = MaterialTheme.typography.titleSmall,
                        modifier = Modifier.padding(start = 16.dp, top = 8.dp, bottom = 4.dp)
                    )
                }
                items(cameras, key = { it.uniqueId }) { camera ->
                    ListItem(
                        headlineContent = { Text(camera.name) },
                        supportingContent = {
                            Column {
                                Text("Topics:")
                                Text(
                                    text = camera.imageTopicName,
                                    maxLines = 1,
                                    overflow = TextOverflow.Ellipsis
                                )
                                Text(
                                    text = camera.compressedImageTopicName,
                                    maxLines = 1,
                                    overflow = TextOverflow.Ellipsis
                                )
                                Text(
                                    text = camera.infoTopicName,
                                    maxLines = 1,
                                    overflow = TextOverflow.Ellipsis
                                )
                            }
                        },
                        trailingContent = {
                            Row(
                                verticalAlignment = Alignment.CenterVertically // <-- Add this right here
                            ) {
                                Switch(
                                    checked = camera.enabled,
                                    onCheckedChange = { onCameraToggle(camera, it) }
                                )
                                Spacer(modifier = Modifier.width(8.dp))
                                Icon(
                                    Icons.Filled.ChevronRight,
                                    contentDescription = "View details",
                                    tint = MaterialTheme.colorScheme.onSurfaceVariant
                                )
                            }
                        },
                        modifier = Modifier.clickable { onCameraClick(camera) }
                    )
                    Divider()
                }
            }
        }
    }
}
