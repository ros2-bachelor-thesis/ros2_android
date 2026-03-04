# Sensors for ROS

Sensors for ROS is an app that publishes sensor data from an Android device onto ROS 2 topics.
Currently it supports ROS Humble.

**Supported sensors**
* Accelerometer
* Barometer
* Camera(s)
* Gyroscope
* Illuminance
* Magnetometer

The app uses a Java/Kotlin + Native hybrid architecture: a Kotlin Activity with Jetpack Compose hosts a `SurfaceView` for the Dear ImGui rendering, while the native layer (`libandroid-ros.so`) handles ROS 2, sensors, and cameras. ROS 2 packages up to `rclcpp` are cross-compiled via a CMake superbuild, then Gradle bundles everything into an APK.

## Inspiration

These projects were extremely helpful, and used as a reference for this one:

* https://github.com/cnlohr/rawdrawandroid
* https://github.com/ocornut/imgui/tree/master/examples/example_android_opengl3
* https://www.sisik.eu/blog/android/ndk/camera

## How to install it

Currently the only way to get **Sensors for ROS** is to build it from source.
It is not yet available on Google's app store.

## How to build it from source

You do not need ROS installed on your machine to build the **Sensors for ROS** app.
However, it's needed to use the sensor data being published by your Android device.
Follow [these instructions to install ROS Humble](https://docs.ros.org/en/humble/Installation.html).

### Computer setup (Nix)

If you have [Nix](https://nixos.org/) with flakes enabled, the dev environment is a single command:

```bash
nix develop
```

This provides the Android SDK (API 33, NDK 25.1, build-tools 33.0.2), Gradle, JDK 17, Python with ROS 2 build deps, and all other tools. It also generates `local.properties` for Gradle automatically.

Skip to [Clone the repo](#clone-the-repo).

### Computer setup (manual)

Download the [Android SDK "Command-line tools only" version](https://developer.android.com/studio#command-tools).
Other versions may work, but this is the minimum needed.

Make a folder for the SDK and extract the archive.

```bash
mkdir ~/android-sdk
cd ~/android-sdk
unzip ~/Downloads/commandlinetools-linux-8512546_latest.zip
```

Install some Android SDK components
(If it gives linkage error try installing `sudo apt install openjdk-17-jre-headless`)
```
./cmdline-tools/bin/sdkmanager --sdk_root=$HOME/android-sdk "build-tools;33.0.2" "platforms;android-33" "ndk;25.1.8937393" "cmake;3.22.1"
```

Install JDK 17 (needed for Gradle and keytool):

```bash
sudo apt install openjdk-17-jdk
```

Install `adb`

```bash
# If you're using Ubuntu
sudo apt install adb android-sdk-platform-tools-common
# If you're using Fedora
sudo dnf install android-tools
```

Install catkin-pkg, empy, and lark

```bash
# If you're using Ubuntu
sudo apt install python3-catkin-pkg-modules python3-empy python3-lark-parser
# If you're using Fedora
sudo dnf install python3-catkin_pkg python3-empy python3-lark-parser
```

Create `local.properties` in the repo root pointing to your SDK:

```bash
echo "sdk.dir=$HOME/android-sdk" > local.properties
```

You may need to do additional setup to use adb.
Follow the [Set up a device for development](https://developer.android.com/studio/run/device#setting-up) instructions if you're using Ubuntu, or follow [the instructions in this thread](https://forums.fedoraforum.org/showthread.php?298965-HowTo-set-up-adb-(Android-Debug-Bridge)-on-Fedora-20) if you're using Fedora.

### Create debug keys

```bash
mkdir -p ~/.android
keytool -genkey -v -keystore ~/.android/debug.keystore -alias androiddebugkey -keyalg RSA -keysize 2048 -validity 10000 -storepass android -keypass android
```

### Clone the repo

The official repo is [`sloretz/sensors_for_ros`](https://github.com/sloretz/sensors_for_ros).

```
git clone https://github.com/sloretz/sensors_for_ros.git
```

Next initialize the git submodules.

```bash
git submodule init
git submodule update
```

### Download ROS dependencies

Use [vcstool](https://github.com/dirk-thomas/vcstool) to download the ROS packages we need to cross compile into the `deps` folder.

```
vcs import --input ros.repos deps/
```

### Building the App

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

### Installing the App on your Android Device

```bash
adb install app/build/outputs/apk/debug/app-debug.apk
```

## Development tips

Use logcat to view the logs from the app
```
adb logcat
```

Sometimes you may want to try out a permission without writing the code to request it.
The app must be installed, but not running already for this command to work.
```
adb shell pm grant com.github.sloretz.sensors_for_ros android.permission.CAMERA
```

The main activity can be started directly from the CLI
```
adb shell am start -n com.github.sloretz.sensors_for_ros/.MainActivity
```

Getting stack traces

```
adb logcat | $ANDROID_HOME/ndk/*/ndk-stack -sym build/jniLibs/arm64-v8a/
```

# Random lessons

During development I documented problems I encountered and fixes for them in the [Problems Encountered](docs/problems_encountered.md) document. Notes on the Java+Native hybrid restructuring are in [Hybrid Restructuring Notes](docs/hybrid_restructuring_notes.md).
