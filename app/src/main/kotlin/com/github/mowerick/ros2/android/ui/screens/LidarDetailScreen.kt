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
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import com.github.mowerick.ros2.android.model.ExternalDeviceInfo
import com.github.mowerick.ros2.android.ui.components.CopyableTextCard

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun LidarDetailScreen(
    device: ExternalDeviceInfo,
    onBack: () -> Unit,
    onConnect: () -> Unit,
    onDisconnect: () -> Unit,
    onEnable: () -> Unit,
    onDisable: () -> Unit
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
            // Connection status
            item {
                CopyableTextCard(
                    label = "Connection Status",
                    displayText = if (device.connected) "Connected" else "Disconnected"
                )
            }

            // Device info
            item {
                CopyableTextCard(
                    label = "USB Path",
                    displayText = device.usbPath
                )
            }

            item {
                CopyableTextCard(
                    label = "Vendor ID",
                    displayText = "0x${device.vendorId.toString(16).uppercase().padStart(4, '0')}"
                )
            }

            item {
                CopyableTextCard(
                    label = "Product ID",
                    displayText = "0x${device.productId.toString(16).uppercase().padStart(4, '0')}"
                )
            }

            // Topic info (only when connected)
            if (device.connected) {
                item {
                    CopyableTextCard(
                        label = "Topic Name",
                        displayText = device.topicName
                    )
                }

                item {
                    CopyableTextCard(
                        label = "Topic Type",
                        displayText = device.topicType
                    )
                }

                item {
                    CopyableTextCard(
                        label = "Publishing Status",
                        displayText = if (device.enabled) "Enabled" else "Disabled"
                    )
                }
            }

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

            // Publishing controls (only when connected)
            if (device.connected) {
                item {
                    Column(
                        modifier = Modifier.fillMaxWidth(),
                        verticalArrangement = Arrangement.spacedBy(8.dp)
                    ) {
                        if (!device.enabled) {
                            Button(
                                onClick = onEnable,
                                modifier = Modifier.fillMaxWidth()
                            ) {
                                Text("Enable Publishing")
                            }
                        } else {
                            OutlinedButton(
                                onClick = onDisable,
                                modifier = Modifier.fillMaxWidth()
                            ) {
                                Text("Disable Publishing")
                            }
                        }
                    }
                }
            }
        }
    }
}
