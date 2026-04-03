package com.github.mowerick.ros2.android.ui.screens

import android.graphics.Bitmap
import androidx.compose.foundation.Image
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
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
import androidx.compose.material3.Tab
import androidx.compose.material3.TabRow
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.unit.dp
import com.github.mowerick.ros2.android.model.NodeState
import com.github.mowerick.ros2.android.model.PipelineNode
import com.github.mowerick.ros2.android.ui.components.CollapsibleCard
import com.github.mowerick.ros2.android.ui.components.NodeStateChip
import com.github.mowerick.ros2.android.ui.components.TopicInfoCard

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun NodeDetailScreen(
    node: PipelineNode,
    canStart: Boolean,
    onBack: () -> Unit,
    onStartStop: () -> Unit,
    runningLocally: Boolean = false,
    detectedOnNetwork: Boolean = false,
    visualizationEnabled: Boolean = false,
    debugFrameRgb: Bitmap? = null,
    debugFrameDepth: Bitmap? = null,
    onEnableVisualization: () -> Unit = {},
    onDisableVisualization: () -> Unit = {}
) {
    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text(node.name) },
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
            // State and start/stop
            item {
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.SpaceBetween,
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    val displayState = if (runningLocally || detectedOnNetwork) NodeState.Running else NodeState.Stopped
                    NodeStateChip(state = displayState)
                    if (!node.isExternal) {
                        when {
                            detectedOnNetwork -> {
                                Text(
                                    text = "Running on Network",
                                    style = MaterialTheme.typography.bodySmall,
                                    color = MaterialTheme.colorScheme.tertiary
                                )
                            }
                            runningLocally -> {
                                OutlinedButton(
                                    onClick = onStartStop,
                                    modifier = Modifier.height(40.dp)
                                ) {
                                    Text("Stop Node")
                                }
                            }
                            else -> {
                                Button(
                                    onClick = onStartStop,
                                    enabled = canStart,
                                    modifier = Modifier.height(40.dp)
                                ) {
                                    Text(if (canStart) "Start Node" else "Waiting for upstream")
                                }
                            }
                        }
                    }
                }
            }

            // Runtime state badge
            item {
                if (node.isExternal) {
                    Card(modifier = Modifier.fillMaxWidth()) {
                        Column(modifier = Modifier.padding(16.dp)) {
                            Text("External Hardware", style = MaterialTheme.typography.titleSmall)
                            Text(
                                text = "This node runs on external hardware (Jetson/PC) and is not managed by this app.",
                                style = MaterialTheme.typography.bodyMedium,
                                color = MaterialTheme.colorScheme.onSurfaceVariant,
                                modifier = Modifier.padding(top = 4.dp)
                            )
                        }
                    }
                } else if (detectedOnNetwork) {
                    Card(modifier = Modifier.fillMaxWidth()) {
                        Column(modifier = Modifier.padding(16.dp)) {
                            Text("Running on Another Device", style = MaterialTheme.typography.titleSmall)
                            Text(
                                text = "This node is running on another Android device or PC on the network.",
                                style = MaterialTheme.typography.bodyMedium,
                                color = MaterialTheme.colorScheme.onSurfaceVariant,
                                modifier = Modifier.padding(top = 4.dp)
                            )
                        }
                    }
                } else if (runningLocally) {
                    Card(modifier = Modifier.fillMaxWidth()) {
                        Column(modifier = Modifier.padding(16.dp)) {
                            Text("Running Locally", style = MaterialTheme.typography.titleSmall)
                            Text(
                                text = "This node is running on this Android device.",
                                style = MaterialTheme.typography.bodyMedium,
                                color = MaterialTheme.colorScheme.primary,
                                modifier = Modifier.padding(top = 4.dp)
                            )
                        }
                    }
                }
            }

            // Perception-specific UI (only for object_detection node running locally)
            if (node.id == "object_detection" && runningLocally) {
                item {
                    CollapsibleCard(
                        title = "Pipeline Info",
                        initiallyExpanded = false
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

                // Visualization toggle and frames
                item {
                    Card(modifier = Modifier.fillMaxWidth()) {
                        Column(modifier = Modifier.padding(16.dp)) {
                            Text("Debug Visualization", style = MaterialTheme.typography.titleMedium)
                            if (visualizationEnabled) {
                                OutlinedButton(
                                    onClick = onDisableVisualization,
                                    modifier = Modifier.fillMaxWidth().padding(top = 8.dp)
                                ) {
                                    Text("Disable Visualization")
                                }
                            } else {
                                Button(
                                    onClick = onEnableVisualization,
                                    modifier = Modifier.fillMaxWidth().padding(top = 8.dp)
                                ) {
                                    Text("Enable Visualization")
                                }
                            }
                        }
                    }
                }

                if (visualizationEnabled) {
                    item {
                        var selectedTab by remember { mutableStateOf(0) }

                        Card(modifier = Modifier.fillMaxWidth()) {
                            Column {
                                TabRow(selectedTabIndex = selectedTab) {
                                    Tab(
                                        selected = selectedTab == 0,
                                        onClick = { selectedTab = 0 },
                                        text = { Text("RGB + Tracks") }
                                    )
                                    Tab(
                                        selected = selectedTab == 1,
                                        onClick = { selectedTab = 1 },
                                        text = { Text("Depth") }
                                    )
                                }

                                when (selectedTab) {
                                    0 -> {
                                        if (debugFrameRgb != null) {
                                            Image(
                                                bitmap = debugFrameRgb.asImageBitmap(),
                                                contentDescription = "RGB with YOLO + Deep SORT",
                                                modifier = Modifier
                                                    .fillMaxWidth()
                                                    .aspectRatio(debugFrameRgb.width.toFloat() / debugFrameRgb.height),
                                                contentScale = ContentScale.Fit
                                            )
                                        } else {
                                            Text(
                                                "Waiting for RGB frame...",
                                                modifier = Modifier.padding(16.dp),
                                                style = MaterialTheme.typography.bodyMedium
                                            )
                                        }
                                    }
                                    1 -> {
                                        if (debugFrameDepth != null) {
                                            Image(
                                                bitmap = debugFrameDepth.asImageBitmap(),
                                                contentDescription = "Depth colormap with YOLO",
                                                modifier = Modifier
                                                    .fillMaxWidth()
                                                    .aspectRatio(debugFrameDepth.width.toFloat() / debugFrameDepth.height),
                                                contentScale = ContentScale.Fit
                                            )
                                        } else {
                                            Text(
                                                "Waiting for depth frame...",
                                                modifier = Modifier.padding(16.dp),
                                                style = MaterialTheme.typography.bodyMedium
                                            )
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // Description
            item {
                Card(modifier = Modifier.fillMaxWidth()) {
                    Column(modifier = Modifier.padding(16.dp)) {
                        Text("Description", style = MaterialTheme.typography.titleSmall)
                        Text(
                            text = node.description,
                            style = MaterialTheme.typography.bodyMedium,
                            modifier = Modifier.padding(top = 4.dp)
                        )
                    }
                }
            }

            // Subscribes to
            if (node.subscribesTo.isNotEmpty()) {
                item {
                    Text("Subscribes to", style = MaterialTheme.typography.titleSmall)
                }
                items(node.subscribesTo.size) { index ->
                    val topic = node.subscribesTo[index]
                    TopicInfoCard(
                        label = "SUB",
                        topicName = topic.name,
                        topicType = topic.type
                    )
                }
            }

            // Publishes to
            if (node.publishesTo.isNotEmpty()) {
                item {
                    Text("Publishes to", style = MaterialTheme.typography.titleSmall)
                }
                items(node.publishesTo.size) { index ->
                    val topic = node.publishesTo[index]
                    TopicInfoCard(
                        label = "PUB",
                        topicName = topic.name,
                        topicType = topic.type
                    )
                }
            }
        }
    }
}
