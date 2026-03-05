package com.github.sloretz.sensors_for_ros.ui.screens

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
import com.github.sloretz.sensors_for_ros.model.CameraInfo
import com.github.sloretz.sensors_for_ros.ui.components.TopicInfoCard

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun CameraDetailScreen(
    camera: CameraInfo,
    onBack: () -> Unit,
    onEnable: () -> Unit,
    onDisable: () -> Unit
) {
    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text(camera.name) },
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
            if (camera.enabled) {
                OutlinedButton(
                    onClick = onDisable,
                    modifier = Modifier.fillMaxWidth().height(48.dp)
                ) {
                    Text("Disable Camera")
                }
            } else {
                Button(
                    onClick = onEnable,
                    modifier = Modifier.fillMaxWidth().height(48.dp)
                ) {
                    Text("Enable Camera")
                }
            }

            TopicInfoCard(
                label = "Image",
                topicName = camera.imageTopicName,
                topicType = camera.imageTopicType
            )

            TopicInfoCard(
                label = "Camera Info",
                topicName = camera.infoTopicName,
                topicType = camera.infoTopicType
            )

            if (camera.enabled && camera.resolutionWidth > 0) {
                Card(modifier = Modifier.fillMaxWidth()) {
                    Text(
                        text = "Resolution: ${camera.resolutionWidth}x${camera.resolutionHeight}",
                        style = MaterialTheme.typography.bodyLarge,
                        modifier = Modifier.padding(16.dp)
                    )
                }
            }
        }
    }
}
