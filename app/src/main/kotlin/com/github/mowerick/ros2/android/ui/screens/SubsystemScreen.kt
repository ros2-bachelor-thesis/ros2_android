package com.github.mowerick.ros2.android.ui.screens

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.ArrowBack
import androidx.compose.material.icons.filled.Refresh
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import com.github.mowerick.ros2.android.model.PipelineNode
import com.github.mowerick.ros2.android.ui.components.PipelineNodeCard
import com.github.mowerick.ros2.android.ui.components.TopicConnector

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun SubsystemScreen(
    nodes: List<PipelineNode>,
    onBack: () -> Unit,
    onNodeClick: (PipelineNode) -> Unit,
    onNodeStartStop: (String) -> Unit,
    isNodeStartable: (String) -> Boolean,
    canProbeNode: (String) -> Boolean,
    isNodeProbing: (String) -> Boolean,
    onToggleNodeProbing: (String) -> Unit,
    onReset: () -> Unit,
    isRunningLocally: (String) -> Boolean = { false },
    isDetectedOnNetwork: (String) -> Boolean = { false },
    isNodeStarting: (String) -> Boolean = { false }
) {
    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text("ROS 2 Subsystem") },
                navigationIcon = {
                    IconButton(onClick = onBack) {
                        Icon(Icons.Filled.ArrowBack, contentDescription = "Back")
                    }
                },
                actions = {
                    IconButton(onClick = onReset) {
                        Icon(Icons.Filled.Refresh, contentDescription = "Reset Pipeline")
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
            verticalArrangement = Arrangement.spacedBy(4.dp)
        ) {
            nodes.forEachIndexed { index, node ->
                item(key = node.id) {
                    PipelineNodeCard(
                        node = node,
                        canStart = isNodeStartable(node.id),
                        canProbe = canProbeNode(node.id),
                        onStartStop = { onNodeStartStop(node.id) },
                        onClick = { onNodeClick(node) },
                        onProbe = { onToggleNodeProbing(node.id) },
                        isProbing = isNodeProbing(node.id),
                        runningLocally = isRunningLocally(node.id),
                        detectedOnNetwork = isDetectedOnNetwork(node.id),
                        isStarting = isNodeStarting(node.id)
                    )
                }

                if (index < nodes.lastIndex) {
                    item(key = "${node.id}_connector") {
                        TopicConnector()
                    }
                }
            }
        }
    }
}
