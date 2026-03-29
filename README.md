# ROS 2 Android

An Android app that deploys a ROS 2 Humble Perception & Positioning subsystem on ARM devices (arm64-v8a) using Eclipse Cyclone DDS. Built on top of [sloretz/sensors_for_ros](https://github.com/sloretz/sensors_for_ros) (Loretz, ROSCon 2022) - a CMake superbuild of ~70 ROS 2 Humble packages for Android - restructured from a pure C++ NativeActivity into a Java/Kotlin + Native hybrid with Jetpack Compose UI.

Target: Android 13 (API 33), NDK 25.1.

## Features

### Implemented

- **Built-in sensor publishers** - accelerometer, barometer, gyroscope, illuminance, magnetometer, GPS location published as ROS 2 topics with frame IDs and timestamps
- **Camera publisher** - front and rear device cameras published as `sensor_msgs/Image` (raw BGR8) and `sensor_msgs/CompressedImage` (JPEG)
- **Wi-Fi multicast / DDS discovery** - `MulticastLock` to enable DDS multicast on Android Wi-Fi
- **DDS domain selection** - configurable `ROS_DOMAIN_ID` and network interface for DDS discovery
- **Event-driven architecture** - JNI callbacks for sensor data and camera frames (zero polling overhead)
- **Notification system** - user notification overlay for alerts and error messages from both native (C++) and Kotlin layers
- **Jetpack Compose UI** - sensor list, live sensor data view, camera preview, node pipeline management
- **Testing framework** - Python-based ROS 2 subscriber test suite with matplotlib visualizers for all sensor types (accelerometer, gyroscope, magnetometer, barometer, illuminance, GPS, camera)

### Planned

- **USB camera** - external USB cameras via libusb/libuvc with JNI file descriptor handoff, published as `sensor_msgs/Image`
- **USB LiDAR** - YDLIDAR SDK integration via JNI fd handoff, published as `sensor_msgs/LaserScan`
- **DDS-Security** - OpenSSL static linking (hidden visibility to avoid BoringSSL collision), Cyclone DDS security plugins, SROS2 credentials
- **Subscriber and in-app visualization** - subscribe to `sensor_msgs/Image` topics and render in the Android UI (replacing rviz, which is infeasible due to Qt5/Ogre3D dependencies)
- **YOLO object detection** - on-device inference via NCNN, subscribing to camera topics and publishing object detections
- **micro-ROS Agent** - hosting the agent on Android to bridge ROS 2 DDS to microcontrollers via serial/USB

## Architecture

```text
┌─────────────────────────────────────────────────┐
│   Kotlin/Java (Jetpack Compose UI)              │
│   - MainActivity, ViewModels, UI Components     │
│   - GpsManager (FusedLocationProviderClient)    │
└─────┬────────────────────────────────┬──────────┘
      │                                │
      │ Camera2 API                    │ JNI Bridge
      │ (Java)                         │ (NativeBridge.kt ↔ jni_bridge.cc)
      │                                │ - jobject construction
      │                                │ - Event callbacks
┌─────▼────────────────────────────────▼──────────┐
│   C++ Native Layer                              │
│   ┌───────────────────────────────────────┐     │
│   │ ROS 2 Interface (rclcpp)              │     │
│   │ - Node lifecycle                      │     │
│   │ - Publisher management                │     │
│   └───────────────┬───────────────────────┘     │
│                   │                             │
│   ┌───────────────▼───────────────────────┐     │
│   │ Controllers                           │     │
│   │ - Sensor controllers (IMU, etc.)      │     │
│   │ - GPS controller (receives via JNI)   │     │
│   │ - Camera controllers (front/back)     │     │
│   └───┬───────────────────────┬───────────┘     │
│       │                       │                 │
│   ┌───▼──────────┐      ┌─────▼────────┐        │
│   │ ASensorEvent │      │ Camera frame │        │
│   │   Queue      │      │   callbacks  │        │
│   │              │      │   (via JNI)  │        │
│   └──────────────┘      └──────────────┘        │
└──────┬──────────────────────────────────────────┘
       │
┌──────▼──────┐
│ ASensor     │
│ Manager     │
│ (NDK)       │
└─────────────┘
       │
       │ Cyclone DDS (UDP multicast discovery + unicast data)
       │
┌─────────▼───────────────────────────────────────────────────┐
│   ROS 2 Network (same domain ID)                            │
│   - Other ROS 2 nodes on host machine                       │
│   - Topics: /<device_id>/sensors/*, /<device_id>/camera/*   │
└─────────────────────────────────────────────────────────────┘
```

The native layer cross-compiles ~70 ROS 2 Humble packages via a CMake superbuild. The Kotlin layer communicates with C++ through JNI, constructing Java objects directly (SensorInfo, SensorReading, CameraInfo, Bitmap) to avoid string serialization overhead. Event callbacks notify the UI layer when sensor data or camera frames are available.

**Data sources:**

- **IMU sensors** (accelerometer, gyroscope, magnetometer, barometer, illuminance) - acquired in C++ via `ASensorManager` (NDK), event queue forwarded to ROS controllers
- **GPS** - acquired in Kotlin via `FusedLocationProviderClient` (Google Play Services - required on device), location updates passed to C++ GPS controller via JNI
- **Cameras** - acquired in Java via `Camera2` API, frames passed to C++ camera controllers via JNI for encoding and publishing

## Published ROS 2 Topics

The app publishes the following topics that can be discovered and consumed by other ROS 2 nodes on the same domain:

**Sensor data:**

- `/<device_id>/sensors/accelerometer` - `sensor_msgs/Imu` - 3-axis acceleration (m/s²)
- `/<device_id>/sensors/gyroscope` - `sensor_msgs/Imu` - 3-axis angular velocity (rad/s)
- `/<device_id>/sensors/magnetometer` - `sensor_msgs/MagneticField` - 3-axis magnetic field (µT)
- `/<device_id>/sensors/barometer` - `sensor_msgs/FluidPressure` - atmospheric pressure (hPa)
- `/<device_id>/sensors/illuminance` - `sensor_msgs/Illuminance` - ambient light (lux)
- `/<device_id>/sensors/gps` - `sensor_msgs/NavSatFix` - GPS location (lat/lon/alt)

**Camera image streams:**

- `/<device_id>/camera/front/image_color` - `sensor_msgs/Image` - front camera raw BGR8 (~1-3 MB/frame)
- `/<device_id>/camera/front/image_color/compressed` - `sensor_msgs/CompressedImage` - front camera JPEG (~50-100 KB/frame)
- `/<device_id>/camera/rear/image_color` - `sensor_msgs/Image` - rear camera raw BGR8 (~1-3 MB/frame)
- `/<device_id>/camera/rear/image_color/compressed` - `sensor_msgs/CompressedImage` - rear camera JPEG (~50-100 KB/frame)

**Camera calibration:**

- `/<device_id>/camera/front/camera_info` - `sensor_msgs/CameraInfo` - front camera intrinsics
- `/<device_id>/camera/rear/camera_info` - `sensor_msgs/CameraInfo` - rear camera intrinsics

> [!NOTE] > `<device_id>` is configurable in the app's ROS Setup screen and defaults to the device's sanitized name (e.g., `pixel_7`, `galaxy_s23`). This namespace allows multiple Android devices to publish on the same ROS 2 network without topic collisions.

All published messages include a `frame_id` field in the header (e.g., `"<device_id>_camera_front"`, `"<device_id>_imu_link"`) and a timestamp indicating when the data was captured. This allows other ROS 2 nodes to transform the data between coordinate frames and temporally synchronize multiple sensors using ROS 2's TF (Transform) system.

## How to Build

You do not need ROS 2 installed on your machine to build the app.

> [!NOTE]
> ROS 2 Humble is needed on a companion machine to interact with the published topics. Follow [these instructions to install ROS Humble](https://docs.ros.org/en/humble/Installation.html).

### Dependencies

**Android SDK Components:**

- Command-line Tools 8.0
- Platform Tools 35.0.2
- Build Tools 33.0.2 and 34.0.0
- Platform API 33 and 34
- NDK 25.1.8937393
- CMake 3.22.1

**Build Tools:**

- JDK 21
- Gradle 8.x (via wrapper)
- GNU Make
- vcstool (for ROS 2 dependency management)
- zip/unzip
- git
- adb (Android Debug Bridge)

**Python 3 Packages:**

- catkin-pkg
- empy 3.3.4 (ROS 2 Humble requires 3.x, not 4.x)
- lark-parser
- pip
- setuptools

### Computer Setup

Download the [Android SDK "Command-line tools only" version](https://developer.android.com/studio#command-tools).

```bash
mkdir ~/android-sdk
cd ~/android-sdk
unzip ~/Downloads/commandlinetools-linux-8512546_latest.zip
```

Install SDK components (if it gives a linkage error try `sudo apt install openjdk-21-jre-headless`):

```bash
./cmdline-tools/bin/sdkmanager --sdk_root=$HOME/android-sdk "build-tools;33.0.2" "build-tools;34.0.0" "platforms;android-33" "platforms;android-34" "ndk;25.1.8937393" "cmake;3.22.1"
```

Install JDK 21:

```bash
sudo apt install openjdk-21-jdk
```

Install adb:

```bash
# Ubuntu
sudo apt install adb android-sdk-platform-tools-common
# Fedora
sudo dnf install android-tools
```

Install Python dependencies:

```bash
# Ubuntu
sudo apt install python3-catkin-pkg-modules python3-empy python3-lark-parser
# Fedora
sudo dnf install python3-catkin_pkg python3-empy python3-lark-parser
```

Create `local.properties` in the repo root pointing to your SDK:

```bash
echo "sdk.dir=$HOME/android-sdk" > local.properties
```

You may need to do additional setup to use adb.
Follow the [Set up a device for development](https://developer.android.com/studio/run/device#setting-up) instructions if you're using Ubuntu, or follow [the instructions in this thread](<https://forums.fedoraforum.org/showthread.php?298965-HowTo-set-up-adb-(Android-Debug-Bridge)-on-Fedora-20>) if you're using Fedora.

### Create Debug Keys

```bash
mkdir -p ~/.android
keytool -genkey -v -keystore ~/.android/debug.keystore -alias adb_debug_key -keyalg RSA -keysize 2048 -validity 10000 -storepass android -keypass android
```

### Clone and Initialize

```bash
git clone https://github.com/ros2-bachelor-thesis/ros2_android.git
cd ros2_android
git submodule init
git submodule update
```

### Download ROS Dependencies

Fetch git submodules and ROS 2 source dependencies:

```bash
make deps
```

This initializes git submodules and uses [vcstool](https://github.com/dirk-thomas/vcstool) to download ~70 ROS 2 packages into `deps/`.

### Build

The build system uses a Makefile with two stages: CMake cross-compiles the native libraries, then Gradle compiles Kotlin and produces the APK.

**Build everything (native + APK):**

```bash
make all
```

**Or build stages separately:**

```bash
# Build native libraries only (CMake cross-compilation)
make native

# Build APK only (requires native libraries already built)
make app
```

**Build variants:**

```bash
# Build with debug symbols (no optimization)
make debug

# Build optimized (no debug symbols)
make release
```

The native libraries are staged to `build/jniLibs/arm64-v8a/` and the APK is produced at `app/build/outputs/apk/debug/app-debug.apk`.

### Install and Run

**Install APK to connected device:**

```bash
make install
```

**Build, install, and launch app:**

```bash
make run
```

## Debug

### Logging

**View app logs (recommended):**

```bash
make logcat
```

This clears the log buffer and tails logs filtered to the app.

**View all device logs:**

```bash
adb logcat -v color
```

**Save logs to file:**

```bash
adb logcat -v time > logcat.txt
```

### Cleaning Builds

**Clean everything:**

```bash
make clean
```

**Clean only app build (keeps native libs):**

```bash
make clean-app
```

**Clean native build (forces full rebuild):**

```bash
make clean-native
```

### Native Stack Traces

Symbolicate native crashes using ndk-stack:

```bash
adb logcat | $ANDROID_HOME/ndk/*/ndk-stack -sym build/jniLibs/arm64-v8a/
```

### Granting Permissions

Grant a permission without the request dialog (app must be installed but not running):

```bash
adb shell pm grant com.github.mowerick.ros2.android android.permission.CAMERA
```

### Quick ROS 2 Network Setup

Use the provided script to configure your host machine for ROS 2 discovery with the Android device:

```bash
# Get Android device IP
adb shell ip addr show wlan0 | grep "inet "

# Run setup script (configures firewall and ROS 2 daemon)
./scripts/setup_ros2_network.sh <android_ip> <domain_id>

# Export domain ID in current terminal
export ROS_DOMAIN_ID=<domain_id>

# Verify topics are visible
ros2 topic list
```

**Example:**

```bash
./scripts/setup_ros2_network.sh 192.168.1.42 1
export ROS_DOMAIN_ID=1
ros2 topic list
```

> [!NOTE]
> The domain ID must match the setting in the Android app (default: 1). For detailed testing instructions and troubleshooting, see the [Testing Guide](scripts/tests/README.md).

## Testing

The Android app publishes sensor and camera data as ROS 2 topics that can be consumed by nodes running on a host machine (Linux/macOS). This enables visualization, logging, and integration with the full ROS 2 ecosystem.

For complete testing instructions, including:

- Automated network setup script
- Interactive sensor testing framework with visualizers
- Manual testing procedures
- Troubleshooting common issues

See the **[Testing Guide](scripts/tests/README.md)**.
