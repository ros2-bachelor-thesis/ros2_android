package com.github.mowerick.ros2.android.ui.screens

import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.ArrowBack
import androidx.compose.material3.Divider
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.ListItem
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import com.github.mowerick.ros2.android.model.ExternalDeviceInfo
import com.github.mowerick.ros2.android.model.ExternalDeviceType

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun ExternalSensorsScreen(
    devices: List<ExternalDeviceInfo>,
    onBack: () -> Unit,
    onLidarClick: (ExternalDeviceInfo) -> Unit
) {
    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text("External Sensors") },
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
            // LIDAR devices section
            val lidarDevices = devices.filter { it.deviceType == ExternalDeviceType.LIDAR }
            if (lidarDevices.isNotEmpty()) {
                item {
                    Text(
                        text = "LiDAR Devices",
                        style = MaterialTheme.typography.titleSmall,
                        modifier = Modifier.padding(start = 16.dp, top = 8.dp, bottom = 4.dp)
                    )
                }
                items(lidarDevices, key = { it.uniqueId }) { device ->
                    ListItem(
                        headlineContent = { Text(device.name) },
                        supportingContent = {
                            Column {
                                Text("Status: ${if (device.connected) "Connected" else "Disconnected"}")
                                Text("USB: ${device.usbPath}")
                                if (device.connected) {
                                    Text("Topic: ${device.topicName}")
                                }
                            }
                        },
                        modifier = Modifier.clickable { onLidarClick(device) }
                    )
                    Divider()
                }
            }

            // USB Camera placeholder section
            item {
                Text(
                    text = "USB Cameras",
                    style = MaterialTheme.typography.titleSmall,
                    modifier = Modifier.padding(start = 16.dp, top = 8.dp, bottom = 4.dp)
                )
            }
            item {
                ListItem(
                    headlineContent = {
                        Text(
                            "USB Camera (Coming Soon)",
                            color = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                    },
                    supportingContent = {
                        Text(
                            "UVC camera support - not yet implemented",
                            color = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                    }
                )
                Divider()
            }

            // Empty state
            if (lidarDevices.isEmpty()) {
                item {
                    Text(
                        text = "No external devices connected",
                        style = MaterialTheme.typography.bodyMedium,
                        modifier = Modifier.padding(16.dp),
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                }
            }
        }
    }
}
