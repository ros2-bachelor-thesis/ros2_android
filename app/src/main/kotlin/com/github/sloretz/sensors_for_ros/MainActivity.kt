package com.github.sloretz.sensors_for_ros

import android.Manifest
import android.content.pm.PackageManager
import android.os.Bundle
import android.view.MotionEvent
import android.view.SurfaceHolder
import android.view.SurfaceView
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.ui.Modifier
import androidx.compose.ui.viewinterop.AndroidView
import androidx.core.content.ContextCompat
import java.net.NetworkInterface

class MainActivity : ComponentActivity() {

    private val requestCameraPermission =
        registerForActivityResult(ActivityResultContracts.RequestPermission()) { granted ->
            NativeBridge.nativeOnPermissionResult("CAMERA", granted)
        }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        NativeBridge.nativeInit(cacheDir.absolutePath, packageName)
        NativeBridge.nativeSetNetworkInterfaces(queryNetworkInterfaces())

        setContent {
            AndroidView(
                modifier = Modifier.fillMaxSize(),
                factory = { context ->
                    SurfaceView(context).apply {
                        holder.addCallback(object : SurfaceHolder.Callback {
                            override fun surfaceCreated(holder: SurfaceHolder) {
                                NativeBridge.nativeSurfaceCreated(holder.surface)
                            }

                            override fun surfaceChanged(
                                holder: SurfaceHolder,
                                format: Int,
                                width: Int,
                                height: Int
                            ) {
                                // No-op: EGL queries surface size directly
                            }

                            override fun surfaceDestroyed(holder: SurfaceHolder) {
                                NativeBridge.nativeSurfaceDestroyed()
                            }
                        })

                        setOnTouchListener { _, event ->
                            NativeBridge.nativeTouchEvent(
                                event.action,
                                event.x,
                                event.y,
                                event.getToolType(0)
                            )
                            true
                        }
                    }
                }
            )
        }

        if (ContextCompat.checkSelfPermission(this, Manifest.permission.CAMERA)
            != PackageManager.PERMISSION_GRANTED
        ) {
            requestCameraPermission.launch(Manifest.permission.CAMERA)
        } else {
            NativeBridge.nativeOnPermissionResult("CAMERA", true)
        }
    }

    override fun onDestroy() {
        NativeBridge.nativeDestroy()
        super.onDestroy()
    }

    private fun queryNetworkInterfaces(): Array<String> {
        return try {
            NetworkInterface.getNetworkInterfaces()
                ?.toList()
                ?.map { it.name }
                ?.toTypedArray()
                ?: emptyArray()
        } catch (e: Exception) {
            emptyArray()
        }
    }
}
