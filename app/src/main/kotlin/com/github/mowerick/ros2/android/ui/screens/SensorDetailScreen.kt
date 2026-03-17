package com.github.mowerick.ros2.android.ui.screens

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.ArrowBack
import androidx.compose.material3.Button
import androidx.compose.material3.Card
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
import com.github.mowerick.ros2.android.model.SensorInfo
import com.github.mowerick.ros2.android.model.SensorReading
import com.github.mowerick.ros2.android.ui.components.CollapsibleCard
import com.github.mowerick.ros2.android.ui.components.CopyableTextCard

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun SensorDetailScreen(
    sensor: SensorInfo,
    reading: SensorReading?,
    onBack: () -> Unit,
    onEnable: () -> Unit,
    onDisable: () -> Unit
) {
    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text(sensor.prettyName) },
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
                .padding(16.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            if (sensor.enabled) {
                OutlinedButton(
                    onClick = onDisable,
                    modifier = Modifier.fillMaxWidth().height(48.dp)
                ) {
                    Text("Disable Sensor")
                }
            } else {
                Button(
                    onClick = onEnable,
                    modifier = Modifier.fillMaxWidth().height(48.dp)
                ) {
                    Text("Enable Sensor")
                }
            }

            CollapsibleCard(
                title = "Sensor Info",
                initiallyExpanded = true
            ) {
                Text("Name: ${sensor.sensorName}", style = MaterialTheme.typography.bodyLarge)
                Text("Vendor: ${sensor.vendor}", style = MaterialTheme.typography.bodyMedium)
            }

            CollapsibleCard(
                title = "Topic",
                initiallyExpanded = true
            ) {
                Text(
                    text = "Name: ${if (sensor.topicName.startsWith("/")) sensor.topicName else "/${sensor.topicName}"}",
                    style = MaterialTheme.typography.bodyMedium
                )
                Text(
                    text = "Type: ${sensor.topicType}",
                    style = MaterialTheme.typography.bodyMedium,
                    modifier = Modifier.padding(top = 4.dp)
                )
            }

            if (reading != null) {
                CopyableTextCard(
                    label = "Last Reading",
                    displayText = reading.formattedValue,
                    copyText = reading.copyableValue
                )
            } else {
                Card(modifier = Modifier.fillMaxWidth()) {
                    Column(modifier = Modifier.padding(16.dp)) {
                        Text("Last Reading", style = MaterialTheme.typography.titleSmall)
                        Text(
                            text = reading?.formattedValue ?: "Waiting for data...",
                            style = MaterialTheme.typography.headlineSmall,
                            modifier = Modifier.padding(top = 8.dp)
                        )
                    }
                }
            }
        }
    }
}
