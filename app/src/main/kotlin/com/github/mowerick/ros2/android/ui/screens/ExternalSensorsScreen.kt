package com.github.mowerick.ros2.android.ui.screens

import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.ArrowBack
import androidx.compose.material.icons.filled.Refresh
import androidx.compose.material3.Button
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
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import com.github.mowerick.ros2.android.model.ExternalDeviceInfo
import com.github.mowerick.ros2.android.model.ExternalDeviceType

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun ExternalSensorsScreen(
    devices: List<ExternalDeviceInfo>,
    onBack: () -> Unit,
    onScanDevices: () -> Unit,
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
            item {
                Text(
                    text = "LiDAR Devices",
                    style = MaterialTheme.typography.titleSmall,
                    modifier = Modifier.padding(start = 16.dp, top = 8.dp, bottom = 4.dp)
                )
            }

            if (lidarDevices.isNotEmpty()) {
                items(lidarDevices, key = { it.uniqueId }) { device ->
                    ListItem(
                        headlineContent = { Text(device.name) },
                        supportingContent = {
                            Column {
                                Text("Status: ${if (device.connected) "Connected" else "Disconnected"}")
                                Text("TTY: ${device.usbPath}")
                                if (device.connected) {
                                    Text("Topic: ${device.topicName}")
                                }
                            }
                        },
                        modifier = Modifier.clickable { onLidarClick(device) }
                    )
                    Divider()
                }
            } else {
                item {
                    Column(
                        modifier = Modifier
                            .fillMaxWidth()
                            .padding(16.dp),
                        horizontalAlignment = Alignment.CenterHorizontally
                    ) {
                        Text(
                            text = "No LIDAR devices detected",
                            style = MaterialTheme.typography.bodyMedium,
                            color = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                        Spacer(modifier = Modifier.padding(4.dp))
                        Text(
                            text = "1. Connect YDLIDAR via USB\n2. Tap the refresh icon above to scan",
                            style = MaterialTheme.typography.bodySmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                        Spacer(modifier = Modifier.padding(8.dp))
                        Button(onClick = onScanDevices) {
                            Icon(Icons.Filled.Refresh, contentDescription = null)
                            Spacer(modifier = Modifier.width(8.dp))
                            Text("Scan for Devices")
                        }
                    }
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
        }
    }
}
