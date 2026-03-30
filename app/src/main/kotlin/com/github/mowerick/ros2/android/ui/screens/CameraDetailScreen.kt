package com.github.mowerick.ros2.android.ui.screens

import android.graphics.Bitmap
import androidx.compose.foundation.Image
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
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
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.unit.dp
import com.github.mowerick.ros2.android.model.CameraInfo
import com.github.mowerick.ros2.android.ui.components.TopicInfoCard

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun CameraDetailScreen(
    camera: CameraInfo,
    cameraFrame: Bitmap?,
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
                .padding(16.dp)
                .verticalScroll(rememberScrollState()),
            verticalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            if (camera.enabled) {
                OutlinedButton(
                    onClick = onDisable,
                    modifier = Modifier.fillMaxWidth().height(48.dp)
                ) {
                    Text("Stop")
                }
            } else {
                Button(
                    onClick = onEnable,
                    modifier = Modifier.fillMaxWidth().height(48.dp)
                ) {
                    Text("Publish")
                }
            }

            if (camera.enabled) {
                // Bitmap is already rotated to portrait orientation by camera_device.cc
                // Use actual bitmap dimensions for aspect ratio
                if (cameraFrame != null) {
                    Image(
                        bitmap = cameraFrame.asImageBitmap(),
                        contentDescription = "Camera preview",
                        modifier = Modifier
                            .fillMaxWidth()
                            .aspectRatio(cameraFrame.width.toFloat() / cameraFrame.height.toFloat()),
                        contentScale = ContentScale.Fit
                    )
                } else {
                    Card(modifier = Modifier.fillMaxWidth()) {
                        Text(
                            text = "Waiting for camera frame...",
                            style = MaterialTheme.typography.bodyMedium,
                            modifier = Modifier.padding(32.dp)
                        )
                    }
                }
            }

            TopicInfoCard(
                label = "Image (Raw)",
                topicName = camera.imageTopicName,
                topicType = camera.imageTopicType
            )

            TopicInfoCard(
                label = "Image (Compressed)",
                topicName = camera.compressedImageTopicName,
                topicType = camera.compressedImageTopicType
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
