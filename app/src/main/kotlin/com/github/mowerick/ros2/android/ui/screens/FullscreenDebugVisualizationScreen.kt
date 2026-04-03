package com.github.mowerick.ros2.android.ui.screens

import android.graphics.Bitmap
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.gestures.detectHorizontalDragGestures
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Close
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.unit.dp
import kotlin.math.abs

@Composable
fun FullscreenDebugVisualizationScreen(
    debugFrameRgb: Bitmap?,
    debugFrameDepth: Bitmap?,
    onBack: () -> Unit
) {
    var selectedTab by remember { mutableStateOf(0) }

    Box(
        modifier = Modifier
            .fillMaxSize()
            .background(Color.Black)
            .pointerInput(Unit) {
                detectHorizontalDragGestures(
                    onDragEnd = {
                        // Swipe to switch between RGB and Depth
                    }
                ) { _, dragAmount ->
                    if (abs(dragAmount) > 50) {
                        selectedTab = if (dragAmount < 0) {
                            1 // Swipe left -> Depth
                        } else {
                            0 // Swipe right -> RGB
                        }
                    }
                }
            }
    ) {
        // Fullscreen image display
        Box(
            modifier = Modifier.fillMaxSize(),
            contentAlignment = Alignment.Center
        ) {
            when (selectedTab) {
                0 -> {
                    if (debugFrameRgb != null) {
                        Image(
                            bitmap = debugFrameRgb.asImageBitmap(),
                            contentDescription = "RGB with YOLO + Deep SORT (Fullscreen)",
                            modifier = Modifier.fillMaxSize(),
                            contentScale = ContentScale.Fit
                        )
                    } else {
                        Text(
                            "Waiting for RGB frame...",
                            color = Color.White,
                            style = MaterialTheme.typography.bodyLarge,
                            modifier = Modifier.padding(16.dp)
                        )
                    }
                }
                1 -> {
                    if (debugFrameDepth != null) {
                        Image(
                            bitmap = debugFrameDepth.asImageBitmap(),
                            contentDescription = "Depth colormap with YOLO (Fullscreen)",
                            modifier = Modifier.fillMaxSize(),
                            contentScale = ContentScale.Fit
                        )
                    } else {
                        Text(
                            "Waiting for depth frame...",
                            color = Color.White,
                            style = MaterialTheme.typography.bodyLarge,
                            modifier = Modifier.padding(16.dp)
                        )
                    }
                }
            }
        }

        // Close button overlay (top-right)
        IconButton(
            onClick = onBack,
            modifier = Modifier
                .align(Alignment.TopEnd)
                .padding(16.dp)
        ) {
            Icon(
                Icons.Filled.Close,
                contentDescription = "Close Fullscreen",
                tint = Color.White
            )
        }

        // View mode indicator (bottom-center)
        Surface(
            modifier = Modifier
                .align(Alignment.BottomCenter)
                .padding(bottom = 24.dp),
            shape = RoundedCornerShape(16.dp),
            color = Color.Black.copy(alpha = 0.6f)
        ) {
            Row(
                modifier = Modifier.padding(horizontal = 16.dp, vertical = 8.dp),
                horizontalArrangement = Arrangement.spacedBy(16.dp)
            ) {
                Text(
                    text = "RGB + Tracks",
                    color = if (selectedTab == 0) Color.White else Color.Gray,
                    style = MaterialTheme.typography.bodyMedium,
                    modifier = Modifier.clickable { selectedTab = 0 }
                )
                Text(
                    text = "Depth",
                    color = if (selectedTab == 1) Color.White else Color.Gray,
                    style = MaterialTheme.typography.bodyMedium,
                    modifier = Modifier.clickable { selectedTab = 1 }
                )
            }
        }
    }
}
