package com.github.mowerick.ros2.android

import android.Manifest
import android.app.Activity
import android.content.Context
import android.content.IntentSender
import android.content.pm.PackageManager
import android.location.Location
import android.location.LocationManager
import android.os.Looper
import android.util.Log
import androidx.activity.result.ActivityResultLauncher
import androidx.activity.result.IntentSenderRequest
import androidx.core.app.ActivityCompat
import com.google.android.gms.common.api.ResolvableApiException
import com.google.android.gms.location.FusedLocationProviderClient
import com.google.android.gms.location.LocationCallback
import com.google.android.gms.location.LocationRequest
import com.google.android.gms.location.LocationResult
import com.google.android.gms.location.LocationServices
import com.google.android.gms.location.LocationSettingsRequest
import com.google.android.gms.location.Priority
import com.google.android.gms.location.SettingsClient

/**
 * Manages GPS location updates using FusedLocationProviderClient
 * and forwards them to native code for ROS publishing
 */
class GpsManager(private val context: Context) {
    private val tag = "GpsManager"
    private val fusedLocationClient: FusedLocationProviderClient =
        LocationServices.getFusedLocationProviderClient(context)
    private val settingsClient: SettingsClient =
        LocationServices.getSettingsClient(context)

    private var locationCallback: LocationCallback? = null
    private var isStarted = false
    private var locationRequest: LocationRequest? = null

    companion object {
        const val REQUEST_CHECK_SETTINGS = 1001
    }

    /**
     * Check if location settings are sufficient and prompt user to enable if not
     * Call this before start() to give user a chance to enable location
     */
    fun checkLocationSettings(
        launcher: ActivityResultLauncher<IntentSenderRequest>,
        onSuccess: () -> Unit,
        onFailure: () -> Unit
    ) {
        val request = createLocationRequest()

        val builder = LocationSettingsRequest.Builder()
            .addLocationRequest(request)
            .setAlwaysShow(true)

        Log.i(tag, "Checking location settings")

        settingsClient.checkLocationSettings(builder.build())
            .addOnSuccessListener {
                Log.i(tag, "Location settings are satisfied")
                onSuccess()
            }
            .addOnFailureListener { exception ->
                if (exception is ResolvableApiException) {
                    try {
                        Log.i(tag, "Location settings not satisfied, showing dialog to user")
                        val intentSenderRequest = IntentSenderRequest.Builder(exception.resolution).build()
                        launcher.launch(intentSenderRequest)
                    } catch (e: IntentSender.SendIntentException) {
                        Log.e(tag, "Error showing location settings dialog: ${e.message}")
                        onFailure()
                    }
                } else {
                    Log.e(tag, "Location settings check failed: ${exception.message}")
                    onFailure()
                }
            }
    }

    /**
     * Start GPS location updates
     * Returns true if started successfully, false otherwise
     */
    fun start(): Boolean {
        if (isStarted) {
            Log.d(tag, "GPS already started")
            return true
        }

        if (!hasLocationPermission()) {
            Log.e(tag, "Cannot start GPS: Location permission not granted")
            return false
        }

        if (!isLocationEnabled()) {
            Log.e(tag, "Cannot start GPS: Location services are disabled on device")
            return false
        }

        Log.i(tag, "Starting GPS location updates...")

        val request = createLocationRequest()
        locationRequest = request

        locationCallback = object : LocationCallback() {
            override fun onLocationResult(locationResult: LocationResult) {
                locationResult.lastLocation?.let { location ->
                    onLocationUpdate(location)
                }
            }
        }

        try {
            fusedLocationClient.requestLocationUpdates(
                request,
                locationCallback!!,
                Looper.getMainLooper()
            )
            isStarted = true
            Log.i(tag, "GPS location updates started - FusedLocationProviderClient is now active")
            Log.i(tag, "Waiting for GPS fix... (may take 10-30 seconds outdoors)")
            return true
        } catch (e: SecurityException) {
            Log.e(tag, "Failed to start GPS: ${e.message}")
            return false
        }
    }

    /**
     * Create location request with desired settings
     */
    private fun createLocationRequest(): LocationRequest {
        return LocationRequest.Builder(
            Priority.PRIORITY_HIGH_ACCURACY,
            1000L  // 1 second update interval
        ).apply {
            setMinUpdateIntervalMillis(500L)  // 0.5 second fastest update
            setWaitForAccurateLocation(false)
        }.build()
    }

    /**
     * Stop GPS location updates
     */
    fun stop() {
        if (!isStarted) {
            Log.d(tag, "GPS not started")
            return
        }

        locationCallback?.let {
            fusedLocationClient.removeLocationUpdates(it)
            locationCallback = null
        }
        isStarted = false
        Log.i(tag, "GPS location updates stopped")
    }

    /**
     * Called when a new location is available
     * Forwards the location to native code via JNI
     */
    private fun onLocationUpdate(location: Location) {
        val latitude = location.latitude
        val longitude = location.longitude
        val altitude = if (location.hasAltitude()) location.altitude else 0.0
        val accuracy = if (location.hasAccuracy()) location.accuracy else 0f
        val altitudeAccuracy = if (location.hasVerticalAccuracy()) {
            location.verticalAccuracyMeters
        } else {
            0f
        }
        val timestampNs = location.elapsedRealtimeNanos

        Log.i(
            tag,
            "GPS Update: lat=$latitude, lon=$longitude, alt=$altitude, acc=${accuracy}m, altAcc=${altitudeAccuracy}m"
        )

        // Forward to native code
        NativeBridge.nativeOnGpsLocation(
            latitude,
            longitude,
            altitude,
            accuracy,
            altitudeAccuracy,
            timestampNs
        )
    }

    /**
     * Check if location permissions are granted
     */
    private fun hasLocationPermission(): Boolean {
        return ActivityCompat.checkSelfPermission(
            context,
            Manifest.permission.ACCESS_FINE_LOCATION
        ) == PackageManager.PERMISSION_GRANTED ||
                ActivityCompat.checkSelfPermission(
                    context,
                    Manifest.permission.ACCESS_COARSE_LOCATION
                ) == PackageManager.PERMISSION_GRANTED
    }

    /**
     * Check if location services are enabled on the device
     */
    private fun isLocationEnabled(): Boolean {
        val locationManager = context.getSystemService(Context.LOCATION_SERVICE) as LocationManager
        return locationManager.isProviderEnabled(LocationManager.GPS_PROVIDER) ||
                locationManager.isProviderEnabled(LocationManager.NETWORK_PROVIDER)
    }

    fun isRunning(): Boolean = isStarted

    /**
     * Get GPS status for diagnostics
     */
    fun getStatus(): String {
        val hasPermission = hasLocationPermission()
        val locationEnabled = isLocationEnabled()

        return when {
            !hasPermission -> "Permission denied"
            !locationEnabled -> "Location services disabled"
            isStarted -> "Running"
            else -> "Stopped"
        }
    }
}
