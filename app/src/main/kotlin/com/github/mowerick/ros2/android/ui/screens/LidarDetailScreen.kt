package com.github.mowerick.ros2.android.ui.screens

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.selection.selectable
import androidx.compose.foundation.selection.selectableGroup
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.ArrowBack
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.RadioButton
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.unit.dp
import com.github.mowerick.ros2.android.model.ExternalDeviceInfo
import com.github.mowerick.ros2.android.ui.components.CollapsibleCard

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun LidarDetailScreen(
    device: ExternalDeviceInfo,
    isBeingToggled: Boolean,
    onBack: () -> Unit,
    onConnect: () -> Unit,
    onDisconnect: () -> Unit,
    onEnable: () -> Unit,
    onDisable: () -> Unit,
    onBaudrateChange: (Int) -> Unit
) {
    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text(device.name) },
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
            // Connection controls
            item {
                Column(
                    modifier = Modifier.fillMaxWidth(),
                    verticalArrangement = Arrangement.spacedBy(8.dp)
                ) {
                    if (!device.connected) {
                        Button(
                            onClick = onConnect,
                            modifier = Modifier.fillMaxWidth()
                        ) {
                            Text("Connect")
                        }
                    } else {
                        OutlinedButton(
                            onClick = onDisconnect,
                            modifier = Modifier.fillMaxWidth()
                        ) {
                            Text("Disconnect")
                        }
                    }
                }
            }

            // Publishing controls (always visible, disabled when not connected or being toggled)
            item {
                Column(
                    modifier = Modifier.fillMaxWidth(),
                    verticalArrangement = Arrangement.spacedBy(8.dp)
                ) {
                    if (!device.enabled) {
                        Button(
                            onClick = onEnable,
                            modifier = Modifier.fillMaxWidth(),
                            enabled = device.connected && !isBeingToggled
                        ) {
                            Text("Publish")
                        }
                    } else {
                        OutlinedButton(
                            onClick = onDisable,
                            modifier = Modifier.fillMaxWidth(),
                            enabled = device.connected && !isBeingToggled
                        ) {
                            Text("Stop")
                        }
                    }
                }
            }

            // Sensor info (USB device details)
            item {
                CollapsibleCard(
                    title = "Sensor Info",
                    initiallyExpanded = true
                ) {
                    Text(
                        text = "USB Path: ${device.usbPath}",
                        style = MaterialTheme.typography.bodyMedium
                    )
                    Text(
                        text = "Vendor ID: 0x${device.vendorId.toString(16).uppercase().padStart(4, '0')}",
                        style = MaterialTheme.typography.bodyMedium,
                        modifier = Modifier.padding(top = 4.dp)
                    )
                    Text(
                        text = "Product ID: 0x${device.productId.toString(16).uppercase().padStart(4, '0')}",
                        style = MaterialTheme.typography.bodyMedium,
                        modifier = Modifier.padding(top = 4.dp)
                    )
                }
            }

                item {
                    CollapsibleCard(
                        title = "Topic",
                        initiallyExpanded = true
                    ) {
                        Text(
                            text = "Name: ${device.topicName}",
                            style = MaterialTheme.typography.bodyMedium
                        )
                        Text(
                            text = "Type: ${device.topicType}",
                            style = MaterialTheme.typography.bodyMedium,
                            modifier = Modifier.padding(top = 4.dp)
                        )
                    }
                }

            // Baudrate selector (always visible, greyed out when connected)
            item {
                Card(
                    modifier = Modifier.fillMaxWidth()
                ) {
                    Column(
                        modifier = Modifier
                            .fillMaxWidth()
                            .padding(16.dp)
                    ) {
                        Text(
                            text = "Serial Baudrate",
                            style = MaterialTheme.typography.titleMedium,
                            modifier = Modifier.padding(bottom = 8.dp)
                        )

                        Column(
                            modifier = Modifier.selectableGroup()
                        ) {
                            device.availableBaudrates.forEach { baudrate ->
                                Row(
                                    modifier = Modifier
                                        .fillMaxWidth()
                                        .selectable(
                                            selected = (baudrate == device.baudrate),
                                            onClick = {
                                                if (!device.connected) {
                                                    onBaudrateChange(baudrate)
                                                }
                                            },
                                            enabled = !device.connected,
                                            role = Role.RadioButton
                                        )
                                        .padding(vertical = 8.dp),
                                    verticalAlignment = Alignment.CenterVertically
                                ) {
                                    RadioButton(
                                        selected = (baudrate == device.baudrate),
                                        onClick = null,
                                        enabled = !device.connected
                                    )
                                    Text(
                                        text = "$baudrate bps",
                                        style = MaterialTheme.typography.bodyLarge,
                                        modifier = Modifier.padding(start = 8.dp),
                                        color = if (device.connected) {
                                            MaterialTheme.colorScheme.onSurface.copy(alpha = 0.38f)
                                        } else {
                                            MaterialTheme.colorScheme.onSurface
                                        }
                                    )
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
