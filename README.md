# ROS 2 Android

An Android app that deploys a ROS 2 Humble Perception & Positioning subsystem on ARM devices (arm64-v8a) using Eclipse Cyclone DDS. Built on top of [sloretz/sensors_for_ros](https://github.com/sloretz/sensors_for_ros) (Loretz, ROSCon 2022) - a CMake superbuild of ~70 ROS 2 Humble packages for Android - restructured from a pure C++ NativeActivity into a Java/Kotlin + Native hybrid with Jetpack Compose UI.

Target: Android 13 (API 33), NDK 25.1.

## Features

### Implemented

- **Built-in sensor publishers** - accelerometer, barometer, gyroscope, illuminance, magnetometer published as ROS 2 topics
- **Camera publisher** - device cameras published as `sensor_msgs/Image`
- **DDS domain selection** - configurable `ROS_DOMAIN_ID` and network interface for DDS discovery
- **Jetpack Compose UI** - sensor list, live sensor data view, camera preview

### Planned

- **Wi-Fi multicast / DDS discovery** - `MulticastLock` to enable DDS multicast on Android Wi-Fi
- **USB camera** - external USB cameras via libusb/libuvc with JNI file descriptor handoff, published as `sensor_msgs/Image`
- **USB LiDAR** - YDLIDAR SDK integration via JNI fd handoff, published as `sensor_msgs/LaserScan`
- **DDS-Security** - OpenSSL static linking (hidden visibility to avoid BoringSSL collision), Cyclone DDS security plugins, SROS2 credentials
- **Subscriber and in-app visualization** - subscribe to `sensor_msgs/Image` topics and render in the Android UI (replacing rviz, which is infeasible due to Qt5/Ogre3D dependencies)
- **YOLO object detection** - on-device inference via NCNN, subscribing to `stereo_image_data` and publishing `object_xyz_pos`
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

You do not need ROS installed on your machine to build the app.
However, ROS 2 Humble is needed on a companion machine to interact with the published topics.
Follow [these instructions to install ROS Humble](https://docs.ros.org/en/humble/Installation.html).

### Dependencies

**Android SDK Components:**
- Android SDK Command-line Tools (version 8.0)
- Platform Tools (version 35.0.2)
- Build Tools (version 33.0.2 and 34.0.0)
- Android Platform API 33 and 34
- NDK 25.1.8937393
- CMake 3.22.1

**Build Tools:**
- JDK 21 (for Gradle and keytool)
- Gradle (downloaded automatically by the wrapper)
- make
- zip/unzip
- git
- adb (Android Debug Bridge)

**Python Packages:**
- catkin-pkg (ROS 2 build dependency)
- empy 3.x (ROS 2 requires 3.x, not 4.x)
- lark-parser
- pip
- setuptools

**ROS 2 Tools:**
- vcstool (for managing ROS 2 package repositories)

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
Follow the [Set up a device for development](https://developer.android.com/studio/run/device#setting-up) instructions if you're using Ubuntu, or follow [the instructions in this thread](https://forums.fedoraforum.org/showthread.php?298965-HowTo-set-up-adb-(Android-Debug-Bridge)-on-Fedora-20) if you're using Fedora.

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

Use [vcstool](https://github.com/dirk-thomas/vcstool) to download the ROS packages needed for cross-compilation:

```bash
vcs import --input ros.repos deps/
```

### Build

The build is two stages: CMake cross-compiles the native libraries, then Gradle compiles Kotlin and produces the APK.

**1. Build native libraries (CMake)**

```bash
cmake -B build -DANDROID_HOME=$ANDROID_HOME
cmake --build build -j$(nproc)
```

This cross-compiles ~70 ROS 2 packages and `libandroid-ros.so`, then stages the shared libraries to `build/jniLibs/arm64-v8a/`.

**2. Build the APK (Gradle)**

```bash
./gradlew :app:assembleDebug
```

The APK is produced at `app/build/outputs/apk/debug/app-debug.apk`.

### Install

```bash
adb install app/build/outputs/apk/debug/app-debug.apk
```

## Debug

### ADB Commands

Start the app:

```bash
adb shell am start -n com.github.mowerick.ros2.android/.MainActivity
```

Reinstall (keeping app data):

```bash
adb install -r app/build/outputs/apk/debug/app-debug.apk
```

### Logging

View all logs:

```bash
adb logcat -v color
```

Filter to this app:

```bash
adb logcat -v color --pid=$(adb shell pidof -s com.github.mowerick.ros2.android)
```

Clear buffer and follow:

```bash
adb logcat -c && adb logcat -v color --pid=$(adb shell pidof -s com.github.mowerick.ros2.android)
```

Save to file:

```bash
adb logcat -v time > logcat.txt
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

## Documentation

- [Hybrid Restructuring Notes](docs/hybrid_restructuring_notes.md) - detailed documentation of the NativeActivity to hybrid conversion, including decisions, workarounds, and issues encountered. This app was restructured from [sloretz/sensors_for_ros](https://github.com/sloretz/sensors_for_ros) (Loretz, ROSCon 2022), which was a pure C++ NativeActivity using Dear ImGui for UI.
