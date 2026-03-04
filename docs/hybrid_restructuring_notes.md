# Hybrid Restructuring Notes

Difficulties and decisions encountered while converting the app from a pure `NativeActivity` (C++ only, `android:hasCode="false"`) to a Java/Kotlin + Native hybrid using Jetpack Compose and Gradle.

## Why the restructuring is necessary

`NativeActivity` prevents receiving Android system intents (e.g., `ACTION_USB_DEVICE_ATTACHED`) and calling Java-only APIs (`UsbManager`, `WifiManager.MulticastLock`). The thesis requires USB sensor integration (LiDAR, camera via `libusb`/`libuvc`) and DDS multicast, both of which need Java-layer access. A standard Kotlin Activity is the prerequisite for all subsequent work.

## JNI function name mangling with underscores in package name

The package name `com.github.sloretz.sensors_for_ros` contains underscores. In JNI C function names, literal underscores in package/class names must be escaped as `_1` (JNI spec section 2.2). So `sensors_for_ros` becomes `sensors_1for_1ros` in every `JNIEXPORT` function:

```c
// Wrong - would resolve to package "com.github.sloretz.sensors.for.ros"
Java_com_github_sloretz_sensors_for_ros_NativeBridge_nativeInit(...)

// Correct
Java_com_github_sloretz_sensors_1for_1ros_NativeBridge_nativeInit(...)
```

Getting this wrong causes `UnsatisfiedLinkError` at runtime with no compile-time warning. The alternative would be to rename the package (breaking the existing namespace), or use `RegisterNatives` in `JNI_OnLoad` instead of relying on name-based resolution.

## LTO stripping JNI entry points

The original build used Link-Time Optimization (`INTERPROCEDURAL_OPTIMIZATION TRUE`) to reduce APK size. With `NativeActivity`, the entry point `ANativeActivity_onCreate` was force-exported via `-u ANativeActivity_onCreate` in the linker flags. After the restructuring, the entry points are JNI functions discovered by the runtime via `dlsym`. LTO could strip them as unused since nothing in the native code calls them directly.

The fix is that `JNIEXPORT` expands to `__attribute__((visibility("default")))`, which prevents LTO from stripping the symbols. The `-u ANativeActivity_onCreate` flag was removed since that symbol no longer exists.

## Two-stage build: CMake then Gradle

The original project had a single CMake build that produced the APK directly using `aapt2`, `zip`, `zipalign`, and `apksigner`. The hybrid approach requires Gradle for Kotlin compilation and Compose, but the CMake superbuild (which cross-compiles ~70 ROS 2 packages) must still run first.

This creates a two-stage build:
1. CMake builds all native dependencies and `libandroid-ros.so`, stages `.so` files to `build/jniLibs/arm64-v8a/`
2. Gradle compiles Kotlin, bundles the pre-built native libs via `jniLibs.srcDirs`, and produces the APK

The coupling point is the `jniLibs` directory. Gradle's `build.gradle.kts` references `${rootProject.projectDir}/build/jniLibs` as its JNI library source. If the CMake build hasn't run, Gradle will produce an APK without native code (no build error, just a runtime crash).

## Input handling: AInputQueue vs direct touch forwarding

The original `GUI` class used `AInputQueue` to poll input events on the draw thread, routing them through `ImGui_ImplAndroid_HandleInputEvent`. This relied on the NativeActivity framework delivering events to a native queue.

With a Kotlin Activity, touch events arrive as `MotionEvent` objects on the UI thread. These are forwarded to native code via JNI (`nativeTouchEvent`), where they're injected directly into ImGui's IO event queue using `ImGuiIO::AddMousePosEvent()` and `ImGuiIO::AddMouseButtonEvent()`.

This is safe because ImGui 1.87+ has a thread-safe IO event queue - touch events arrive on the UI thread while the draw loop runs on a separate thread. The `AInputQueue` mechanism, the `iqueue_mtx_` mutex, `CheckInput()`, `SetInputQueue()`, and `RemoveInputQueue()` were all removed.

One subtlety: `MotionEvent.ACTION_DOWN` (0), `ACTION_UP` (1), and `ACTION_MOVE` (2) happen to have identical numeric values to the NDK `AMOTION_EVENT_ACTION_*` constants, so no translation is needed. However, this is a coincidence rather than a guaranteed API contract.

## Surface lifecycle mapping

The old `NativeActivity` callbacks `onNativeWindowCreated`/`onNativeWindowDestroyed` map directly to `SurfaceHolder.Callback.surfaceCreated`/`surfaceDestroyed`. The native window handle is obtained via `ANativeWindow_fromSurface(env, surface)` instead of being passed directly by the framework.

The `SurfaceView` is embedded inside Jetpack Compose via `AndroidView`, which adds a layer of indirection. The surface lifecycle is tied to the Compose composition - if the `AndroidView` leaves the composition, the surface is destroyed.

## Removal of JNI-based Android API access

The original `jvm.cc` contained ~200 lines of manual JNI code to call Android Java APIs from native code: `GetPackageName`, `HasPermission`, `RequestPermission`, `GetCacheDir`, `GetNetworkInterfaces`. Each function manually attached/detached threads, looked up classes and methods by name, and handled JNI error states.

In the hybrid architecture, all of these are trivial Kotlin calls:
- `cacheDir.absolutePath` replaces 20 lines of JNI
- `NetworkInterface.getNetworkInterfaces()` replaces 30 lines of JNI iteration with a brute-force index loop (0-255)
- `ActivityResultContracts.RequestPermission()` replaces the unreliable `requestPermissions` JNI call that had no callback mechanism (the original code polled `HasPermission` on resume)

The `jvm.h/cc` was reduced to just `SetJavaVM`/`GetJavaVM` for any future native code that may need JNI access.

## Camera permission flow

The original app called `RequestPermission` via JNI in the constructor and then polled `HasPermission` in `onResume` to detect when the user granted camera access. This was fragile - there was no actual callback, just a hope that `onResume` would fire after the permission dialog.

The Kotlin Activity uses `registerForActivityResult(ActivityResultContracts.RequestPermission())` which provides a proper callback. The result is forwarded to native code via `nativeOnPermissionResult("CAMERA", granted)`, which calls `StartCameras()` directly.

## Gradle version constraints

The Nix-provided Gradle (8.14.4) is newer than what the Gradle wrapper requests (8.5). The wrapper is used to generate the initial `gradle-wrapper.jar` and `gradle-wrapper.properties`, after which all builds use `./gradlew` which downloads and uses exactly Gradle 8.5. This avoids compatibility issues with Android Gradle Plugin 8.2.2, which may not support newer Gradle versions.

The `settings.gradle.kts` initially used `dependencyResolution {}` (a newer Gradle API) instead of `dependencyResolutionManagement {}`, causing a build failure. This is the kind of error that only surfaces at Gradle evaluation time, not at the Kotlin compile step.

## Nix store is read-only: AGP auto-download fails

AGP 8.2.2 requires build-tools 34.0.0 as a minimum. It ignores any `buildToolsVersion` pin below that and attempts to auto-download the required version. Since the Nix store is read-only (`/nix/store/...`), this download fails with `The SDK directory is not writable`.

The fix is to provide all required SDK components in `flake.nix`:
- `buildToolsVersions = [ "33.0.2" "34.0.0" ]` (34.0.0 for AGP, 33.0.2 for legacy CMake aapt2 references)
- `platformVersions = [ "33" "34" ]` (34 required because `androidx.activity-compose:1.8.2` requires `compileSdk >= 34`)

`compileSdk` was bumped to 34 in `app/build.gradle.kts`. This only affects compilation (which APIs are visible); `minSdk` and `targetSdk` remain at 33 so the app still targets Android 13.

Similarly, `local.properties` (which points Gradle to the SDK) must be regenerated on every `nix develop` because the Nix store path changes whenever SDK components change. The shellHook unconditionally writes `sdk.dir=$ANDROID_HOME` to avoid stale paths.

## Debug keystore alias mismatch

Gradle's default debug signing config expects the key alias `androiddebugkey`. The existing keystore (created for the old CMake/apksigner flow) used alias `adb_debug_key`. This caused `KeytoolException: Failed to read key AndroidDebugKey` at the `packageDebug` step.

Fixed by adding an explicit `signingConfigs` block in `app/build.gradle.kts` that references the correct alias.
