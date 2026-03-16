# ROS 2 Android

An Android app that deploys a ROS 2 Humble Perception & Positioning subsystem on ARM devices (arm64-v8a) using Eclipse Cyclone DDS. Built on top of [sloretz/sensors_for_ros](https://github.com/sloretz/sensors_for_ros) (Loretz, ROSCon 2022) - a CMake superbuild of ~70 ROS 2 Humble packages for Android - restructured from a pure C++ NativeActivity into a Java/Kotlin + Native hybrid with Jetpack Compose UI.

Target: Android 13 (API 33), NDK 25.1.

## Features

### Implemented

- **Built-in sensor publishers** - accelerometer, barometer, gyroscope, illuminance, magnetometer, GPS location published as ROS 2 topics
- **Camera publisher** - front and rear device cameras published as `sensor_msgs/Image` (raw BGR8) and `sensor_msgs/CompressedImage` (JPEG)
- **Wi-Fi multicast / DDS discovery** - `MulticastLock` to enable DDS multicast on Android Wi-Fi
- **DDS domain selection** - configurable `ROS_DOMAIN_ID` and network interface for DDS discovery
- **Event-driven architecture** - JNI callbacks for sensor data and camera frames (zero polling overhead)
- **Jetpack Compose UI** - sensor list, live sensor data view, camera preview, node pipeline management

### Planned

- **USB camera** - external USB cameras via libusb/libuvc with JNI file descriptor handoff, published as `sensor_msgs/Image`
- **USB LiDAR** - YDLIDAR SDK integration via JNI fd handoff, published as `sensor_msgs/LaserScan`
- **DDS-Security** - OpenSSL static linking (hidden visibility to avoid BoringSSL collision), Cyclone DDS security plugins, SROS2 credentials
- **Subscriber and in-app visualization** - subscribe to `sensor_msgs/Image` topics and render in the Android UI (replacing rviz, which is infeasible due to Qt5/Ogre3D dependencies)
- **YOLO object detection** - on-device inference via NCNN, subscribing to camera topics and publishing object detections
- **micro-ROS Agent** - hosting the agent on Android to bridge ROS 2 DDS to microcontrollers via serial/USB

## Architecture

```
Kotlin (Jetpack Compose UI)
    |
    | JNI (JSON over strings)
    |
C++ (rclcpp, Cyclone DDS, sensor drivers)
    |
    | UDP multicast
    |
ROS 2 network (other nodes on same domain)
```

The native layer cross-compiles ~70 ROS 2 Humble packages via a CMake superbuild. The Kotlin layer communicates with C++ through JNI functions that exchange JSON strings - avoiding fragile `jobject` construction while keeping data volumes trivial.

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
git clone https://github.com/mowerick/ros2_android.git
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

### ROS 2 Topic Inspection

The app publishes ROS 2 topics that can be discovered and inspected from a companion machine on the same network.

**Required environment setup:**

```bash
export ROS_DOMAIN_ID=1
```

**Common commands:**

List all topics:

```bash
ros2 topic list
```

Show topic info (includes QoS settings):

```bash
ros2 topic info /camera/front/image_color -v
```

Echo camera info (lightweight):

```bash
ros2 topic echo /camera/front/camera_info
```

Echo camera images (high bandwidth):

```bash
ros2 topic echo /camera/front/image_color --qos-reliability best_effort
```

Check message rate:

```bash
ros2 topic hz /camera/front/image_color
```

**Network requirements:**

- Android device and companion machine must be on the same network
- Cyclone DDS uses UDP multicast for discovery (port 7650 for domain 1)
- Cyclone DDS uses UDP unicast for data (dynamic port range 7410-7900)
- Firewall must allow incoming UDP traffic on these ports
- The `cyclonedds.xml` config specifies which network interface to use (default: `enp39s0`)

## Testing with ROS 2 Nodes on Host Machine

The Android app publishes sensor and camera data as ROS 2 topics that can be consumed by nodes running on a host machine (Linux/macOS). This enables visualization, logging, and integration with the full ROS 2 ecosystem.

### Network Prerequisites

> [!IMPORTANT]
> Multicast must be enabled for DDS discovery to work between the Android device and your laptop/PC.

DDS discovery relies on UDP multicast (destination address 239.255.0.1). For nodes on different machines to discover each other, the network must support IGMP (Internet Group Management Protocol), which allows routers to intelligently forward multicast packets only to ports where applications have subscribed to the multicast group.

**Common issues:**

- **IGMP snooping disabled**: Some routers disable IGMP snooping by default, causing multicast packets to either flood all ports (inefficient) or be dropped entirely. This prevents DDS discovery across subnets.
- **WiFi multicast filtering**: Many consumer and enterprise WiFi access points aggressively filter multicast traffic to reduce airtime usage, blocking DDS discovery packets even when IGMP is enabled.
- **Firewall blocking multicast**: Host firewalls may drop incoming multicast packets by default.

**Required firewall configuration on host machine:**

Execute the following commands on your testing machine to allow IGMP (multicast group joins) and UDP packets:

```bash
sudo iptables -I INPUT 1 -p igmp -j ACCEPT
sudo iptables -I INPUT 1 -p udp -d 224.0.0.0/4 -j ACCEPT
sudo iptables -I INPUT 1 -p udp -s <source_ip> -j ACCEPT
sudo iptables -L INPUT -v -n --line-numbers
```

> [!TIP]
> Replace `<source_ip>` with your Android device's IP address. Check it with:
>
> ```bash
> adb shell ip addr show wlan0 | grep inet
> ```

**Additional checks:**

1. **Enable IGMP snooping** on your router (check admin interface under "Multicast" or "IGMP" settings).
2. **Verify both devices are on the same subnet** (e.g., both have 192.168.1.x addresses).

### Environment Setup on Host Machine

Set the DDS configuration and domain ID to match the Android app:

```bash
export ROS_DOMAIN_ID=1
```

### Verifying Discovery

List all discovered topics (should include topics from the Android app like `/camera/front/image_color`):

```bash
ros2 topic list
```

Check topic details and QoS settings:

```bash
ros2 topic info /camera/front/image_color -v
```

Measure message publication rate:

```bash
ros2 topic hz /camera/front/image_color
```

### Testing Built-in Sensors

> [!IMPORTANT]
> Before testing sensors, ensure multicast is enabled on your host machine. See [Network Prerequisites](#network-prerequisites) above for firewall configuration.

#### Quick Start

#### Testing Cameras (Front and Rear)

**Required ROS 2 Humble packages** on your testing machine:

```bash
sudo apt install ros-humble-rqt-image-view ros-humble-image-transport ros-humble-compressed-image-transport
```

**Launch the image viewer:**

```bash
ros2 run rqt_image_view rqt_image_view
```

**Available topics:**

- `/camera/front/image_color` - Front camera (raw BGR8)
- `/camera/front/image_color/compressed` - Front camera (JPEG)
- `/camera/back/image_color` - Rear camera (raw BGR8)
- `/camera/back/image_color/compressed` - Rear camera (JPEG)

> [!WARNING]
> Switching between compressed and raw topics (or vice versa) congests the DDS participant. For a smooth camera feed, **restart the publisher** (toggle the camera off and on in the app) after changing topics in rqt_image_view.

**Troubleshooting:**

- **No topics visible**: Check that `ROS_DOMAIN_ID` matches on both devices (default is 1 in the app).
- **Topics listed but no data**: Camera may not be active in the app. Start the camera publisher from the UI.
- **High latency**: Camera images are large (~1-3 MB/frame for raw, 50-100 KB for compressed). Ensure both devices are on 5 GHz WiFi or wired Ethernet.
- **QoS mismatch**: The camera publisher uses `best_effort` reliability. Subscribers must match:

  ```bash
  ros2 topic echo /camera/front/image_color --qos-reliability best_effort
  ```

### Other Available Topics

**Sensor data:**

- `/sensors/accelerometer` - IMU acceleration data
- `/sensors/gyroscope` - IMU angular velocity data
- `/sensors/magnetometer` - Magnetic field data
- `/sensors/barometer` - Atmospheric pressure data
- `/sensors/illuminance` - Light sensor data
- `/sensors/gps` - GPS location data

**Camera info:**

- `/camera/front/camera_info` - Front camera intrinsic calibration
- `/camera/back/camera_info` - Rear camera intrinsic calibration

Echo a topic to inspect data:

```bash
ros2 topic echo /sensors/accelerometer
```

## Documentation

- [Hybrid Restructuring Notes](docs/hybrid_restructuring_notes.md) - detailed documentation of the NativeActivity to hybrid conversion, including decisions, workarounds, and issues encountered. This app was restructured from [sloretz/sensors_for_ros](https://github.com/sloretz/sensors_for_ros) (Loretz, ROSCon 2022), which was a pure C++ NativeActivity using Dear ImGui for UI.
