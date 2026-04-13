package com.github.mowerick.ros2.android.ui.screens

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.AccountTree
import androidx.compose.material.icons.filled.Cable
import androidx.compose.material.icons.filled.BugReport
import androidx.compose.material.icons.filled.RemoveRedEye
import androidx.compose.material.icons.filled.Sensors
import androidx.compose.material.icons.filled.Settings
import androidx.compose.material.icons.filled.Warning
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import com.github.mowerick.ros2.android.ui.components.SectionCard

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun DashboardScreen(
    rosStarted: Boolean,
    rosDomainId: Int,
    sensorCount: Int,
    cameraCount: Int,
    externalDeviceCount: Int,
    onSettingsClick: () -> Unit,
    onBuiltInSensorsClick: () -> Unit,
    onExternalSensorsClick: () -> Unit,
    onSubsystemClick: () -> Unit,
    onBeetlePredatorClick: () -> Unit = {}
) {
    val titleText = if (rosDomainId >= 0) "Dashboard (ID: $rosDomainId)" else "Dashboard (ID: --)"

    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text(titleText) },
                actions = {
                    IconButton(onClick = onSettingsClick) {
                        Icon(Icons.Filled.Settings, contentDescription = "ROS Settings")
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
            if (!rosStarted) {
                item {
                    Card(
                        modifier = Modifier.fillMaxWidth(),
                        colors = CardDefaults.cardColors(
                            containerColor = MaterialTheme.colorScheme.errorContainer
                        )
                    ) {
                        Column(
                            modifier = Modifier.padding(16.dp),
                            verticalArrangement = Arrangement.spacedBy(8.dp)
                        ) {
                            Icon(
                                Icons.Filled.Warning,
                                contentDescription = null,
                                tint = MaterialTheme.colorScheme.onErrorContainer
                            )
                            Text(
                                text = "ROS not configured",
                                style = MaterialTheme.typography.titleMedium,
                                color = MaterialTheme.colorScheme.onErrorContainer
                            )
                            Text(
                                text = "Set the Domain ID and network interface to start ROS 2.",
                                style = MaterialTheme.typography.bodyMedium,
                                color = MaterialTheme.colorScheme.onErrorContainer
                            )
                            Spacer(modifier = Modifier.height(4.dp))
                            Button(onClick = onSettingsClick) {
                                Text("Open Settings")
                            }
                        }
                    }
                }
            }
            item {
                SectionCard(
                    icon = Icons.Filled.Sensors,
                    title = "Built-in Sensors",
                    subtitle = if (rosStarted) "$sensorCount sensors, $cameraCount cameras"
                               else "Start ROS to access sensors",
                    enabled = rosStarted,
                    onClick = onBuiltInSensorsClick
                )
            }
            item {
                SectionCard(
                    icon = Icons.Filled.Cable,
                    title = "External Sensors",
                    subtitle = if (rosStarted) "$externalDeviceCount devices"
                               else "Start ROS to access external sensors",
                    enabled = rosStarted,
                    onClick = onExternalSensorsClick
                )
            }
            item {
                SectionCard(
                    icon = Icons.Filled.AccountTree,
                    title = "ROS 2 Subsystem",
                    subtitle = if (rosStarted) "Perception & Positioning Pipeline"
                               else "Start ROS to access subsystem",
                    enabled = rosStarted,
                    onClick = onSubsystemClick
                )
            }
            item {
                SectionCard(
                    icon = Icons.Filled.BugReport,
                    title = "Beetle Predator",
                    subtitle = if (rosStarted) "Camera + GPS pest detection"
                               else "Start ROS to access",
                    enabled = rosStarted,
                    onClick = onBeetlePredatorClick
                )
            }
        }
    }
}
