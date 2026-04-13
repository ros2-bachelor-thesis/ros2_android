# ROS 2 Android

An Android app that deploys a ROS 2 Humble Perception & Positioning subsystem on ARM devices (arm64-v8a) using Eclipse Cyclone DDS. Built on top of [sloretz/sensors_for_ros](https://github.com/sloretz/sensors_for_ros) (Loretz, ROSCon 2022) - a CMake superbuild of ~70 ROS 2 Humble packages for Android - restructured from a pure C++ NativeActivity into a Java/Kotlin + Native hybrid with Jetpack Compose UI.

Target: Android 13 (API 33), NDK 26.3.

## Features

### Implemented

- **Built-in sensor publishers** - accelerometer, barometer, gyroscope, illuminance, magnetometer, GPS location published as ROS 2 topics with frame IDs and timestamps
- **Camera publisher** - front and rear device cameras published as `sensor_msgs/Image` (raw BGR8) and `sensor_msgs/CompressedImage` (JPEG)
- **USB LiDAR** - YDLIDAR SDK integration via JNI fd handoff, published as `sensor_msgs/LaserScan`
- **YOLOv9 object detection** - on-device inference via NCNN (YOLOv9-s + Deep SORT tracker) for Colorado Potato Beetle detection (beetle, larva, eggs) with 3D localization from ZED camera point clouds
- **State machine pipeline** - sequential ROS 2 subsystem with dynamic node detection (local vs external execution), automatic dependency progression, and distributed deployment support
- **Wi-Fi multicast / DDS discovery** - `MulticastLock` to enable DDS multicast on Android Wi-Fi
- **DDS domain selection** - configurable `ROS_DOMAIN_ID` and network interface for DDS discovery
- **Event-driven architecture** - JNI callbacks for sensor data and camera frames (zero polling overhead)
- **Notification system** - user notification overlay for alerts and error messages from both native (C++) and Kotlin layers
- **Jetpack Compose UI** - sensor list, live sensor data view, camera preview, pipeline node management with runtime state visualization
- **Testing framework** - Python-based ROS 2 subscriber test suite with matplotlib visualizers for all sensor types
- **Beetle Predator mode** - handheld pest detection using built-in rear camera + GPS. Runs NCNN YOLOv9 + Deep SORT on camera frames, publishes geolocated detections as `vermin_collector_ros_msgs/BeetleDetection` with novelty filtering (only new confirmed tracks). User selects which classes (beetle, larva, eggs) trigger publishing via label filter chips
- **Target manager** - CPB egg selection with IMU-based orientation calibration, subscribes to detection results and ZED IMU, publishes pan/tilt goals for the arm commander
- **Arm commander** - pan/tilt arm control with ACK/NACK protocol and state machine (IDLE - AWAITING_ACK - AWAITING_DONE - WAIT_AFTER_DONE), subscribes to position goals, publishes `/PointNShoot` commands for micro-ROS

### Planned (Not Yet Implemented)

- **DDS-Security** - OpenSSL static linking (hidden visibility to avoid BoringSSL collision), Cyclone DDS security plugins, SROS2 credentials
- **USB camera** - external USB cameras via libusb/libuvc with JNI file descriptor handoff, published as `sensor_msgs/Image`
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
│   │ - Publisher / Subscriber management   │     │
│   └───────────────┬───────────────────────┘     │
│                   │                             │
│   ┌───────────────▼───────────────────────┐     │
│   │ Sensor Controllers                    │     │
│   │ - IMU (accel, gyro, mag, baro, lux)   │     │
│   │ - GPS (receives via JNI)              │     │
│   │ - Camera (front/back via Camera2)     │     │
│   │ - LiDAR (YDLIDAR via USB serial)      │     │
│   └───┬───────────────────────┬───────────┘     │
│       │                       │                 │
│   ┌───▼──────────┐      ┌─────▼────────┐        │
│   │ ASensorEvent │      │ Camera frame │        │
│   │   Queue      │      │   callbacks  │        │
│   │              │      │   (via JNI)  │        │
│   └──────────────┘      └──────────────┘        │
│                                                 │
│   ┌─────────────────────────────────────────┐   │
│   │ Perception & Positioning Pipeline       │   │
│   │                                         │   │
│   │ ZED (external) ──► Object Detection     │   │
│   │   (sub: rgb, depth,   (NCNN YOLOv9-s   │   │
│   │    point cloud)        + Deep SORT)     │   │
│   │                           │             │   │
│   │                    ┌──────▼──────┐      │   │
│   │                    │Target Mgr   │      │   │
│   │                    │(sub: eggs,  │      │   │
│   │                    │ IMU, fb)    │      │   │
│   │                    └──────┬──────┘      │   │
│   │                    ┌──────▼──────┐      │   │
│   │                    │Arm Commander│      │   │
│   │                    │(pub: PnS,   │      │   │
│   │                    │ sub: ACK)   │      │   │
│   │                    └─────────────┘      │   │
│   └─────────────────────────────────────────┘   │
│                                                 │
│   ┌─────────────────────────────────────────┐   │
│   │ Beetle Predator (handheld detection)    │   │
│   │ Rear camera ──► NCNN YOLOv9 + Deep SORT │   │
│   │ + GPS location ──► BeetleDetection msg  │   │
│   └─────────────────────────────────────────┘   │
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
│   - Topics: /<device_id>/sensors/*, /<device_id>/camera/*,  │
│     /scan, /cpb_*, /arm_position_*, /PointNShoot,           │
│     /cpb_predator/detection                   │
└─────────────────────────────────────────────────────────────┘
```

The native layer cross-compiles ~70 ROS 2 Humble packages via a CMake superbuild. The Kotlin layer communicates with C++ through JNI, constructing Java objects directly (SensorInfo, SensorReading, CameraInfo, Bitmap) to avoid string serialization overhead. Event callbacks notify the UI layer when sensor data or camera frames are available.

**Data sources:**

- **IMU sensors** (accelerometer, gyroscope, magnetometer, barometer, illuminance) - acquired in C++ via `ASensorManager` (NDK), event queue forwarded to ROS controllers
- **GPS** - acquired in Kotlin via `FusedLocationProviderClient` (Google Play Services - required on device), location updates passed to C++ GPS controller via JNI
- **Cameras** - acquired in Java via `Camera2` API, frames passed to C++ camera controllers via JNI for encoding and publishing
- **USB LiDAR** - YDLIDAR connected via USB serial (JNI fd handoff from Kotlin USB Host API), scan data published by C++ LiDAR controller
- **Beetle Predator** - rear camera frames (RGBA via `GetLastFrame()`) + GPS location (`GetLastLocation()`) combined with NCNN inference, publishes `vermin_collector_ros_msgs/BeetleDetection` for confirmed Deep SORT tracks

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

**LiDAR:**

- `/scan` - `sensor_msgs/LaserScan` - YDLIDAR scan data (range, angle, intensity)

**Pipeline (perception & positioning):**

- `/<device_id>/cpb_beetle_center` - `geometry_msgs/Point` - 3D beetle detection center
- `/<device_id>/cpb_beetle` - `sensor_msgs/PointCloud2` - cropped beetle point cloud
- `/<device_id>/cpb_larva_center` - `geometry_msgs/Point` - 3D larva detection center
- `/<device_id>/cpb_larva` - `sensor_msgs/PointCloud2` - cropped larva point cloud
- `/<device_id>/cpb_eggs_center` - `geometry_msgs/Point` - 3D egg detection center
- `/<device_id>/cpb_eggs` - `sensor_msgs/PointCloud2` - cropped egg point cloud
- `/arm_position_goal` - `std_msgs/Float32MultiArray` - pan/tilt goal from target manager
- `/PointNShoot` - `std_msgs/Float32MultiArray` - pan/tilt command to microcontroller
- `/arm_position_feedback` - `std_msgs/String` - arm commander state feedback

**Beetle Predator (handheld detection):**

- `/cpb_predator/detection` - `vermin_collector_ros_msgs/BeetleDetection` - geolocated pest detection (GPS + 2D bbox + class label + track ID)

> [!NOTE] > `<device_id>` is configurable in the app's ROS Setup screen and defaults to the device's sanitized name (e.g., `pixel_7`, `galaxy_s23`). This namespace allows multiple Android devices to publish on the same ROS 2 network without topic collisions.

All published messages include a `frame_id` field in the header (e.g., `"<device_id>_camera_front"`, `"<device_id>_imu_link"`) and a timestamp indicating when the data was captured. This allows other ROS 2 nodes to transform the data between coordinate frames and temporally synchronize multiple sensors using ROS 2's TF (Transform) system.

## Perception & Positioning Subsystem

The app includes a complete perception and positioning pipeline for Colorado Potato Beetle (CPB) detection and localization. The pipeline operates as a state machine with sequential dependency progression.

### Pipeline Architecture

```
ZED Camera (External) → Object Detection (Android) → Target Manager → Arm Commander → micro-ROS Agent
```

**State Machine Flow:**

```
STOPPED → ZED_PROBING → ZED_AVAILABLE → DETECTION_RUNNING →
TARGET_RUNNING → COMMAND_ACTIVE
```

### Object Detection Node

**Input (subscribed topics from external ZED camera):**

- `/zed/zed_node/rgb/image_rect_color/compressed` - JPEG compressed RGB image
- `/zed/zed_node/depth/depth_registered` - Depth map aligned to RGB
- `/zed/zed_node/point_cloud/cloud_registered` - 3D point cloud

**Processing pipeline:**

1. JPEG decompression via libjpeg-turbo (TurboJPEG)
2. YOLOv9-s detection via NCNN (1280×736 letterbox input)
3. Deep SORT multi-object tracking with MARS ReID features
4. 3D localization by querying point cloud at detection center
5. Point cloud cropping to detection bounding box

**Output (published topics):**

- `/<device_id>/cpb_beetle_center` - `geometry_msgs/Point` - 3D center location of beetle detections
- `/<device_id>/cpb_beetle` - `sensor_msgs/PointCloud2` - Cropped point cloud for beetle
- `/<device_id>/cpb_larva_center` - `geometry_msgs/Point` - 3D center location of larva detections
- `/<device_id>/cpb_larva` - `sensor_msgs/PointCloud2` - Cropped point cloud for larva
- `/<device_id>/cpb_eggs_center` - `geometry_msgs/Point` - 3D center location of egg detections
- `/<device_id>/cpb_eggs` - `sensor_msgs/PointCloud2` - Cropped point cloud for eggs

**Detection classes:**

- `cpb_beetle` (class 0) - Adult Colorado Potato Beetle
- `cpb_larva` (class 1) - CPB larvae
- `cpb_eggs` (class 2) - CPB egg clusters

**Performance:**

- Model size: YOLOv9-s (~19 MB) + MARS-small128 (~5.4 MB)
- Input resolution: 1280×736
- Feature dimension: 128-D appearance features for tracking
- Inference backend: NCNN (Tencent) optimized for ARM NEON

### Target Manager Node

Selects CPB egg targets for laser engagement, compensating for camera-to-laser physical offsets and device orientation via ZED IMU data.

**State machine:** INIT - CALIBRATING - READY - SENT_TARGET - WAITING_TO_RETURN - RETURNING - FINISHED (also supports FIXED_POSITION_MODE for manual override)

**Input (subscribed topics):**

- `/cpb_eggs_center` - `geometry_msgs/Point` - 3D egg cluster location from object detection
- `/zed/zed_node/imu/data` - `sensor_msgs/Imu` - ZED camera IMU for orientation calibration
- `/arm_position_feedback` - `std_msgs/String` - state feedback from arm commander
- `/pan_tilt_fixed_position` - `std_msgs/Float32MultiArray` - manual override position

**Output (published topics):**

- `/arm_position_goal` - `std_msgs/Float32MultiArray` - computed pan/tilt angles for arm commander

### Arm Commander Node

Manages the pan/tilt arm command protocol with the microcontroller via micro-ROS. Implements a state machine with timeout-based retransmission and NACK handling.

**State machine:** IDLE - AWAITING_ACK - AWAITING_DONE - WAIT_AFTER_DONE - WAIT_AFTER_NACK - NACK_LIMIT_EXCEEDED

**Input (subscribed topics):**

- `/arm_position_goal` - `std_msgs/Float32MultiArray` - pan/tilt goal from target manager
- `/PointNShoot_ACK` - `std_msgs/Float32` - acknowledgment from microcontroller
- `/PointNShoot_DONE` - `std_msgs/Float32` - completion signal from microcontroller
- `/PointNShoot_NACK` - `std_msgs/Float32` - negative acknowledgment from microcontroller

**Output (published topics):**

- `/PointNShoot` - `std_msgs/Float32MultiArray` - pan/tilt command to microcontroller
- `/arm_position_feedback` - `std_msgs/String` - current state name (consumed by target manager)

### Dynamic Node Detection

The pipeline supports distributed deployment across multiple Android devices or PCs. Nodes can run locally or be detected as running on other devices via topic probing.

**Node states:**

- **Stopped** - Node not running anywhere
- **Running Locally** - Node executing on this Android device
- **Running on Network** - Node detected on another device (topics discovered via DDS)
- **External Hardware** - Node runs on dedicated hardware (e.g., ZED camera on Jetson)

**User workflow:**

1. Navigate to "ROS 2 Subsystem" from dashboard
2. Probe topics → discovers ZED camera
3. Start object detection locally → loads NCNN models, publishes detections
4. Click object detection card → view live statistics (total detections, active tracks, queue size)
5. Downstream nodes become startable as upstream dependencies are satisfied

> [!TIP]
> The object detection node can run on Phone A while target manager runs on Phone B. Topic discovery automatically detects which nodes are running where and enables/disables start buttons accordingly.

### Technical Implementation Notes

**OpenCV dependency isolation:**

- OpenCV (opencv-mobile) is **only** linked in the perception library (`libros2_android_perception.so`)
- The main app (`libandroid-ros.so`) uses libjpeg-turbo for JPEG decompression instead of `cv::imdecode()`
- Simple `Point3f` and `Rect` structs replace OpenCV types in the main app
- Perception library exposes raw RGB buffer API: `ProcessFrame(uint8_t*, width, height)`
- This reduces APK size by ~5-6 MB and avoids OpenCV dependency conflicts

**State machine implementation:**

- Pipeline state stored as enum: `PipelineState` (STOPPED → COMMAND_ACTIVE)
- Node runtime tracking: `NodeRuntimeState` (runningLocally, detectedOnNetwork)
- Topic probing drives state transitions automatically
- One-way progression with rollback on node stop (cascades to downstream)
- UI reflects state in real-time (disabled styling, runtime badges)

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
- NDK 26.3.11579264
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
./cmdline-tools/bin/sdkmanager --sdk_root=$HOME/android-sdk "build-tools;33.0.2" "build-tools;34.0.0" "platforms;android-33" "platforms;android-34" "ndk;26.3.11579264" "cmake;3.22.1"
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

### Model Assets Setup

The object detection perception system requires NCNN model files that are not included in this repository (not yet open-sourced). You must manually provide the following model files in the `app/src/main/assets/models/` directory:

**Required files:**

- `yolov9_s_pobed.ncnn.param` (70 KB) - YOLOv9-s detection model parameters
- `yolov9_s_pobed.ncnn.bin` (19 MB) - YOLOv9-s detection model weights
- `osnet_ain_x1_0.ncnn.param` (9.2 KB) - MARS ReID model parameters for tracking
- `osnet_ain_x1_0.ncnn.bin` (5.4 MB) - MARS ReID model weights for tracking

**Model specifications:**

- **Detection model**: YOLOv9-s trained on Colorado Potato Beetle dataset (3 classes: `cpb_beetle`, `cpb_larva`, `cpb_eggs`)
- **Input size**: 1280×736 (letterbox resize with padding)
- **ReID model**: MARS-small128 for Deep SORT multi-object tracking
- **Feature dimension**: 128-D appearance features

Create the directory and copy your model files:

```bash
mkdir -p app/src/main/assets/models
cp /path/to/your/models/*.ncnn.{param,bin} app/src/main/assets/models/
```

> [!NOTE]
> The perception pipeline subscribes to external ZED camera topics (`/zed/zed_node/rgb/image_rect_color/compressed`, `/zed/zed_node/depth/depth_registered`, `/zed/zed_node/point_cloud/cloud_registered`) and publishes 3D-localized detections. The ZED camera should be running on a separate machine on the same ROS 2 network.

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

### Helpful commands for testing

Publish imu data if not available

```bash
ros2 topic pub /zed/zed_node/imu/data sensor_msgs/msg/Imu "{
  header: {
    stamp: {sec: 0, nanosec: 0},
    frame_id: 'hello_world'
  },
  orientation: {x: 0.0, y: 0.0, z: 0.0, w: 1.0},
  angular_velocity: {x: 0.0, y: 0.0, z: 0.0},
  linear_acceleration: {x: 0.0, y: 0.0, z: 0.0}
}"
```

Publish explicit topic from rosbag

```bash
ros2 bag play . --topic /zed/zed_node/rgb/image_rect_color/compressed
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
