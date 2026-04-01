package com.github.mowerick.ros2.android.viewmodel.managers

import android.Manifest
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.content.IntentSender
import android.content.pm.PackageManager
import android.location.Location
import android.location.LocationManager
import android.os.Build
import android.os.Looper
import android.util.Log
import androidx.activity.result.ActivityResultLauncher
import androidx.activity.result.IntentSenderRequest
import androidx.core.app.ActivityCompat
import com.github.mowerick.ros2.android.util.NativeBridge
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
 * and forwards them to native code for ROS publishing.
 *
 * Fully encapsulates GPS logic including permission checks, settings verification,
 * and user prompts. Exposes simple start()/stop() interface with callbacks.
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
    private var locationServiceReceiver: BroadcastReceiver? = null

    // Callbacks for external coordination
    private var onError: ((String) -> Unit)? = null
    private var onPermissionNeeded: (() -> Unit)? = null
    private var onSettingsNeeded: ((ActivityResultLauncher<IntentSenderRequest>) -> Unit)? = null
    private var onLocationServiceDisabled: (() -> Unit)? = null
    private var onLocationServiceEnabled: (() -> Unit)? = null

    companion object {
        const val REQUEST_CHECK_SETTINGS = 1001
    }

    /**
     * Set callbacks for error reporting and permission/settings requests
     */
    fun setCallbacks(
        onError: (String) -> Unit,
        onPermissionNeeded: () -> Unit,
        onSettingsNeeded: (ActivityResultLauncher<IntentSenderRequest>) -> Unit,
        onLocationServiceDisabled: (() -> Unit)? = null,
        onLocationServiceEnabled: (() -> Unit)? = null
    ) {
        this.onError = onError
        this.onPermissionNeeded = onPermissionNeeded
        this.onSettingsNeeded = onSettingsNeeded
        this.onLocationServiceDisabled = onLocationServiceDisabled
        this.onLocationServiceEnabled = onLocationServiceEnabled
    }

    /**
     * Start GPS with automatic permission and settings checks.
     * This is the primary entry point for starting GPS.
     *
     * If permissions are missing, calls onPermissionNeeded callback.
     * If settings need adjustment, calls onSettingsNeeded callback with launcher.
     * If any error occurs, calls onError callback.
     * If successful, starts location updates immediately.
     */
    fun startWithChecks(launcher: ActivityResultLauncher<IntentSenderRequest>?): Boolean {
        Log.i(tag, "startWithChecks() called")

        // Check permission first
        if (!hasLocationPermission()) {
            Log.w(tag, "No location permission, requesting...")
            onPermissionNeeded?.invoke()
            return false
        }

        // Check location settings
        if (launcher != null) {
            checkLocationSettings(
                launcher,
                onSuccess = {
                    Log.i(tag, "Settings OK, starting GPS")
                    if (!start()) {
                        onError?.invoke("Failed to start GPS location updates")
                    }
                },
                onFailure = {
                    Log.e(tag, "Settings check failed")
                    onError?.invoke("GPS: Location settings check failed")
                }
            )
            return true // Will start asynchronously if settings are OK
        } else {
            // No launcher, try direct start
            Log.w(tag, "No settings launcher, attempting direct start")
            return if (start()) {
                true
            } else {
                onError?.invoke(getStatus())
                false
            }
        }
    }

    /**
     * Check if location settings are sufficient and prompt user to enable if not
     * Internal method used by startWithChecks()
     */
    private fun checkLocationSettings(
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

            // Register broadcast receiver to monitor location service state changes
            registerLocationServiceReceiver()

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
     * Register broadcast receiver to monitor location service state changes
     */
    private fun registerLocationServiceReceiver() {
        if (locationServiceReceiver != null) {
            Log.d(tag, "Location service receiver already registered")
            return // Already registered
        }

        locationServiceReceiver = object : BroadcastReceiver() {
            override fun onReceive(receiverContext: Context?, intent: Intent?) {
                Log.d(tag, "BroadcastReceiver.onReceive() called - action: ${intent?.action}")

                when (intent?.action) {
                    LocationManager.MODE_CHANGED_ACTION,
                    LocationManager.PROVIDERS_CHANGED_ACTION -> {
                        Log.i(tag, "Location provider state changed - checking if enabled")
                        val isEnabled = isLocationEnabled()
                        Log.i(tag, "Location enabled: $isEnabled")

                        if (!isEnabled) {
                            Log.w(tag, "Location services disabled externally")
                            onLocationServiceDisabled?.invoke()
                        } else {
                            Log.i(tag, "Location services re-enabled externally")
                            onLocationServiceEnabled?.invoke()
                        }
                    }
                    else -> {
                        Log.d(tag, "Received broadcast with action: ${intent?.action}")
                    }
                }
            }
        }

        val filter = IntentFilter().apply {
            addAction(LocationManager.MODE_CHANGED_ACTION)
            addAction(LocationManager.PROVIDERS_CHANGED_ACTION)
        }

        // Register receiver with appropriate flags for Android 13+
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            context.registerReceiver(
                locationServiceReceiver,
                filter,
                Context.RECEIVER_NOT_EXPORTED
            )
        } else {
            context.registerReceiver(locationServiceReceiver, filter)
        }

        Log.i(tag, "Registered location service state monitor (MODE_CHANGED + PROVIDERS_CHANGED)")
    }

    /**
     * Unregister broadcast receiver
     */
    private fun unregisterLocationServiceReceiver() {
        locationServiceReceiver?.let {
            try {
                context.unregisterReceiver(it)
                locationServiceReceiver = null
                Log.i(tag, "Unregistered location service state monitor")
            } catch (e: IllegalArgumentException) {
                Log.w(tag, "Receiver was not registered: ${e.message}")
            }
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

        // Unregister location service state monitor
        unregisterLocationServiceReceiver()

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