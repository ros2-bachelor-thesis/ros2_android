# Hybrid Restructuring Notes

Difficulties and decisions encountered while converting the app from a pure `NativeActivity` (C++ only, `android:hasCode="false"`) to a Java/Kotlin + Native hybrid using Jetpack Compose and Gradle.

### NativeActivity to Hybrid Restructuring

**Why**: `NativeActivity` prevents receiving Android system intents (e.g., `ACTION_USB_DEVICE_ATTACHED`) and calling Java-only APIs (`UsbManager`, `WifiManager.MulticastLock`). The thesis requires USB sensor integration (LiDAR, camera via `libusb`/`libuvc`) and DDS multicast, both of which need Java-layer access. A standard Kotlin Activity is the prerequisite for all subsequent work.
**Approach**: Converted from pure `NativeActivity` (C++ only, `android:hasCode="false"`) to a Java/Kotlin + Native hybrid using Jetpack Compose and Gradle.
**Alternatives**: Keeping `NativeActivity` and using JNI callbacks to access Java APIs was considered but rejected - it would require manually reimplementing every Java API call in C++ with fragile JNI code, and still cannot receive system intents.
**Limitations**: The two-stage build (CMake then Gradle) adds complexity. Gradle cannot invoke the CMake superbuild automatically, so the native libs must be pre-built.
**Issues encountered**: See the specific entries below for JNI mangling, LTO stripping, Gradle constraints, and other issues that arose during the restructuring.
**References**: sensors_for_ros (Loretz, ROSCon 2022), Android NativeActivity documentation.

### JNI Function Name Mangling

**Why**: After renaming the package from `com.github.sloretz.sensors_for_ros` to `com.github.mowerick.ros2.android`, JNI function names needed updating.
**Approach**: The new package has no underscores, so JNI function names use straightforward dot-to-underscore mapping:

```c
// Old (underscores in package name required _1 escaping per JNI spec section 2.2)
Java_com_github_sloretz_sensors_1for_1ros_NativeBridge_nativeInit(...)

// New (no underscores in package name, no escaping needed)
Java_com_github_mowerick_ros2_android_NativeBridge_nativeInit(...)
```

**Alternatives**: None - JNI name mangling rules are fixed by the JNI specification.
**Limitations**: Getting the mangling wrong causes `UnsatisfiedLinkError` at runtime with no compile-time warning.
**Issues encountered**: The `_1` escape sequence in the old names was easy to overlook. The JNI spec (section 2.2) defines that underscores in package/class names must be escaped as `_1`, which makes names with underscores error-prone.
**References**: JNI specification section 2.2 (name mangling rules).

### LTO Stripping JNI Entry Points

**Why**: The original build used Link-Time Optimization (`INTERPROCEDURAL_OPTIMIZATION TRUE`) to reduce APK size. After restructuring, the entry points changed from `ANativeActivity_onCreate` (force-exported via `-u` linker flag) to JNI functions discovered at runtime via `dlsym`.
**Approach**: `JNIEXPORT` expands to `__attribute__((visibility("default")))`, which prevents LTO from stripping the symbols. The `-u ANativeActivity_onCreate` flag was removed since that symbol no longer exists.
**Alternatives**: Could have disabled LTO entirely, but that would increase APK size unnecessarily.
**Limitations**: Any new JNI function must use `JNIEXPORT` or LTO will strip it silently - no build error, just a runtime `UnsatisfiedLinkError`.
**Issues encountered**: LTO could strip JNI functions as unused since nothing in the native code calls them directly. The failure mode is silent at build time and only manifests as a runtime crash.
**References**: GCC/Clang `__attribute__((visibility))` documentation.

### Two-Stage Build: CMake then Gradle

**Why**: The hybrid approach requires Gradle for Kotlin compilation and Compose, but the CMake superbuild (which cross-compiles ~70 ROS 2 packages) must still run first.
**Approach**: Two-stage build:
1. CMake builds all native dependencies and `libandroid-ros.so`, stages `.so` files to `build/jniLibs/arm64-v8a/`
2. Gradle compiles Kotlin, bundles the pre-built native libs via `jniLibs.srcDirs`, and produces the APK

The coupling point is the `jniLibs` directory. Gradle's `build.gradle.kts` references `${rootProject.projectDir}/build/jniLibs` as its JNI library source.
**Alternatives**: The original project had a single CMake build that produced the APK directly using `aapt2`, `zip`, `zipalign`, and `apksigner`. This cannot support Kotlin/Compose.
**Limitations**: If the CMake build hasn't run, Gradle will produce an APK without native code (no build error, just a runtime crash).
**Issues encountered**: No automatic dependency between the two stages - the developer must remember to run CMake before Gradle.
**References**: Android Gradle Plugin documentation on `jniLibs.srcDirs`.

### Replacing Dear ImGui with Jetpack Compose

**Why**: The first hybrid version used Dear ImGui (C++ immediate-mode GUI) via a `SurfaceView` embedded in Compose. This required maintaining a parallel OpenGL rendering pipeline alongside Compose, fragile touch forwarding between UI and draw threads, and all future UI work (USB device dialogs, multicast controls) would need C++/ImGui rather than standard Android components.
**Approach**: Replaced ImGui with native Jetpack Compose screens. Architecture change:

```
BEFORE: Kotlin SurfaceView -> C++ EGL/ImGui draw thread -> Controller.DrawFrame() -> ImGui
AFTER:  Kotlin Compose screens <-> JNI data queries/commands <-> C++ data model
```

**C++ side:** A new `SensorDataProvider` base class replaces `Controller`. It keeps `UniqueId()` and `PrettyName()` but replaces `DrawFrame()` with `GetLastMeasurementJson()` and metadata accessors (`SensorName()`, `SensorVendor()`, `TopicName()`, `TopicType()`). Each sensor controller now protects its `last_msg_` with a `std::mutex` and serializes readings as JSON (e.g., `{"values":[x,y,z],"unit":"m/s^2"}`).

**JNI bridge:** New functions return JSON strings for Kotlin to parse:
- `nativeStartRos(domainId, networkInterface)` - replaces the `RosDomainIdChanged` event
- `nativeGetSensorList()` - returns JSON array of sensor metadata
- `nativeGetSensorData(uniqueId)` - returns latest measurement as JSON
- `nativeGetCameraList()` - returns JSON array of camera metadata
- `nativeEnableCamera(uniqueId)` / `nativeDisableCamera(uniqueId)`
- `nativeGetNetworkInterfaces()` - returns stored interfaces as JSON array

**Kotlin side:** A `RosViewModel` manages navigation state via a `Screen` sealed class and exposes `StateFlow`s for sensors, cameras, and the current reading. It polls `nativeGetSensorData()` at ~10 Hz (100ms delay) for the currently viewed sensor. Four Compose screens replace the four ImGui controllers: `DomainIdScreen`, `SensorListScreen`, `SensorDetailScreen`, `CameraDetailScreen`.
**Alternatives**: Keeping ImGui would avoid the rewrite, but every future feature (USB dialogs, multicast controls, DDS security UI) would require C++ UI code instead of standard Android components.
**Limitations**: JSON serialization adds a small overhead per poll cycle, though data volumes are tiny (a few sensors at 10 Hz, each returning a few hundred bytes).
**Issues encountered**: The C++ EGL/ImGui draw thread, EGL context management, and ImGui font/scaling setup were unnecessary complexity once Compose was available.
**References**: Jetpack Compose documentation, Dear ImGui repository.

#### What was removed

- `gui.h/cc` - EGL display initialization, OpenGL rendering, ImGui setup/teardown, draw thread
- `controller.h` - base class with `DrawFrame()` pure virtual
- `display_topic.h` - ImGui helper template for rendering ROS topic info
- `list_controller.{h,cc}` - ImGui sensor list with navigation events
- `ros_domain_id_controller.{h,cc}` - ImGui numeric keypad and network interface dropdown
- `src/DearImGui/` submodule - the ImGui library itself
- `imgui` static library target from CMakeLists.txt, along with EGL/GLESv3 link dependencies
- `nativeSurfaceCreated`, `nativeSurfaceDestroyed`, `nativeTouchEvent` JNI functions
- GUI navigation event system (`GuiNavigateBack`, `GuiNavigateTo`, controller stack)

### JSON over JNI

**Why**: Need a data exchange format between C++ sensor data and Kotlin UI.
**Approach**: Returning JSON strings from C++ avoids fragile `jobject` construction in JNI code. Parsing in Kotlin with `org.json` (already in the Android SDK) is trivial. Data volumes are tiny - a few sensors polled at 10 Hz, each returning a few hundred bytes of JSON.
**Alternatives**: Constructing Java objects in C++ via `NewObject`/`SetObjectField`/`CallMethod` would require looking up class and method IDs, managing local references, and handling exceptions - all for data that can be expressed as a short string.
**Limitations**: JSON parsing is slower than direct object construction, but irrelevant at these data volumes.
**Issues encountered**: None significant - JSON serialization was straightforward.
**References**: Android `org.json` API documentation.

### Event-Driven Callbacks (Replaced Polling)

**Why**: The initial hybrid architecture used polling - the ViewModel ran coroutine loops calling `nativeGetSensorData()` every 100ms. This wasted CPU cycles even when no new data arrived, drained battery with continuous background coroutines, and added 100ms latency between sensor reading and UI update.

**Approach**: Replaced polling with event-driven JNI callbacks that mirror the existing notification callback infrastructure:

**Native Layer:**
- Created `SensorDataCallbackQueue` and `CameraFrameCallbackQueue` singleton managers in `core/` (similar to `NotificationQueue`)
- Each callback queue implements built-in 10 Hz throttling (100ms minimum interval) to prevent callback storms from high-rate sensors
- Added `PostSensorDataUpdate(sensor_id)` and `PostCameraFrameUpdate(camera_id)` convenience functions
- Updated all sensor controllers (`accelerometer`, `gyroscope`, `barometer`, `illuminance`, `magnetometer`, `gps`) to call `PostSensorDataUpdate()` in their `OnSensorReading()` methods
- Updated `CameraController::OnImage()` to call `PostCameraFrameUpdate()` when new frame arrives
- Added JNI registration methods `Java_..._nativeSetSensorDataCallback()` and `Java_..._nativeSetCameraFrameCallback()` in `jni_bridge.cc`
- Callbacks handle thread attachment (`AttachCurrentThread`/`DetachCurrentThread`) automatically when invoked from ROS executor threads
- Global JNI references managed with cleanup on re-registration (prevents memory leaks)

**Kotlin Layer:**
- Added `setSensorDataCallback()` and `setCameraFrameCallback()` methods to `NativeBridge`
- Registered callbacks in `RosViewModel.init{}` block (same pattern as notification callbacks)
- Callbacks check current screen state and only fetch data if the relevant detail screen is active (screen-aware updates)
- Removed polling coroutines and associated state flags
- Removed polling start/stop functions (~50 lines)
- Simplified navigation methods - callbacks handle updates automatically

**Data Flow (Before):**
```
Sensor event → Controller stores in last_msg_
                     ↓
Kotlin coroutine polls every 100ms → JNI → GetSensorData() → return last_msg_
```

**Data Flow (After):**
```
Sensor event → Controller stores in last_msg_ → PostSensorDataUpdate(id)
                                                       ↓
                                         Callback invoked (throttled to 10 Hz)
                                                       ↓
                               Kotlin checks screen state → if detail screen active:
                                                       ↓
                                         JNI → GetSensorData() → return last_msg_
```

**Alternatives**: The initial polling approach was chosen to avoid JNI callback complexity, but after implementing notification callbacks successfully, the pattern was proven safe and could be reused for sensor/camera data.
**Limitations**: UI updates still capped at 10 Hz (same as before), but now updates arrive immediately when data is available rather than on a fixed 100ms schedule. Sensors still publish to ROS topics at their native rate - the throttling only affects UI callbacks.
**Issues encountered**: None - the callback infrastructure worked correctly on first build. The existing notification callback pattern provided a proven template for thread-safe JNI callbacks.
**References**: Android JNI threading documentation (`AttachCurrentThread`), ROS 2 executor threading model, existing `NotificationQueue` implementation in `core/notification_queue.h`.

### Removal of JNI-Based Android API Access

**Why**: The original `jvm.cc` contained ~200 lines of manual JNI code to call Android Java APIs from native code: `GetPackageName`, `HasPermission`, `RequestPermission`, `GetCacheDir`, `GetNetworkInterfaces`. Each function manually attached/detached threads, looked up classes and methods by name, and handled JNI error states.
**Approach**: In the hybrid architecture, all of these are trivial Kotlin calls:
- `cacheDir.absolutePath` replaces 20 lines of JNI
- `NetworkInterface.getNetworkInterfaces()` replaces 30 lines of JNI iteration with a brute-force index loop (0-255)
- `ActivityResultContracts.RequestPermission()` replaces the unreliable `requestPermissions` JNI call that had no callback mechanism

The `jvm.h/cc` was reduced to just `SetJavaVM`/`GetJavaVM` for any future native code that may need JNI access.
**Alternatives**: None - the hybrid architecture makes JNI-based Android API access unnecessary.
**Limitations**: Any future native code that needs Android APIs must go through Kotlin rather than calling Java directly from C++.
**Issues encountered**: The original permission polling code polled `HasPermission` on resume, hoping `onResume` would fire after the permission dialog - a fragile approach with no actual callback.
**References**: Android `ActivityResultContracts` documentation.

### Camera Permission Flow

**Why**: The original app called `RequestPermission` via JNI in the constructor and then polled `HasPermission` in `onResume` to detect when the user granted camera access. This was fragile - there was no actual callback, just a hope that `onResume` would fire after the permission dialog.
**Approach**: The Kotlin Activity uses `registerForActivityResult(ActivityResultContracts.RequestPermission())` which provides a proper callback. The result is forwarded to native code via `nativeOnPermissionResult("CAMERA", granted)`, which calls `StartCameras()` directly.
**Alternatives**: None practical - the `ActivityResultContracts` API is the standard modern Android approach.
**Limitations**: None.
**Issues encountered**: The original JNI-based approach had no real callback mechanism, making the grant detection timing-dependent.
**References**: Android `ActivityResultContracts.RequestPermission` documentation.

### Gradle Version Constraints

**Why**: The Nix-provided Gradle (8.14.4) is newer than what the Gradle wrapper requests (8.5).
**Approach**: The wrapper is used to generate the initial `gradle-wrapper.jar` and `gradle-wrapper.properties`, after which all builds use `./gradlew` which downloads and uses exactly Gradle 8.5. This avoids compatibility issues with Android Gradle Plugin 8.2.2, which may not support newer Gradle versions.
**Alternatives**: Could use the Nix-provided Gradle directly, but risking AGP compatibility issues.
**Limitations**: Pinned to Gradle 8.5 - upgrading requires verifying AGP compatibility.
**Issues encountered**: The `settings.gradle.kts` initially used `dependencyResolution {}` (a newer Gradle API) instead of `dependencyResolutionManagement {}`, causing a build failure. This error only surfaces at Gradle evaluation time, not at the Kotlin compile step.
**References**: Android Gradle Plugin compatibility matrix.

### Nix Store Read-Only: AGP Auto-Download Fails

**Why**: AGP 8.2.2 requires build-tools 34.0.0 as a minimum. It ignores any `buildToolsVersion` pin below that and attempts to auto-download the required version. Since the Nix store is read-only (`/nix/store/...`), this download fails with `The SDK directory is not writable`.
**Approach**: Provide all required SDK components in `flake.nix`:
- `buildToolsVersions = [ "33.0.2" "34.0.0" ]` (34.0.0 for AGP, 33.0.2 for legacy CMake aapt2 references)
- `platformVersions = [ "33" "34" ]` (34 required because `androidx.activity-compose:1.8.2` requires `compileSdk >= 34`)

`compileSdk` was bumped to 34 in `app/build.gradle.kts`. This only affects compilation (which APIs are visible); `minSdk` and `targetSdk` remain at 33 so the app still targets Android 13.

`local.properties` (which points Gradle to the SDK) must be regenerated on every `nix develop` because the Nix store path changes whenever SDK components change. The shellHook unconditionally writes `sdk.dir=$ANDROID_HOME` to avoid stale paths.
**Alternatives**: Could abandon Nix and install the Android SDK manually, but that would lose reproducible builds.
**Limitations**: Every SDK component must be explicitly declared in `flake.nix` - AGP cannot auto-download missing components.
**Issues encountered**: AGP silently overrides `buildToolsVersion` pins below its minimum, making the failure non-obvious until the download attempt hits the read-only filesystem.
**References**: Nix Android SDK overlay documentation, AGP build-tools requirements.

### Debug Keystore Alias Mismatch

**Why**: Gradle's default debug signing config expects the key alias `androiddebugkey`. The existing keystore (created for the old CMake/apksigner flow) used alias `adb_debug_key`.
**Approach**: Added an explicit `signingConfigs` block in `app/build.gradle.kts` that references the correct alias.
**Alternatives**: Could regenerate the keystore with the expected alias, but that would invalidate any existing debug installs.
**Limitations**: None.
**Issues encountered**: The error `KeytoolException: Failed to read key AndroidDebugKey` appeared at the `packageDebug` step - the error message names the expected alias but doesn't mention the actual alias in the keystore.
**References**: Android debug signing documentation.

### Compose Version Constraints

**Why**: The Kotlin compiler extension version (1.5.14) pins the Compose runtime. This constrains which Compose library versions are compatible.
**Approach**: Pinned to compatible versions:
- `material3:1.1.2` - not 1.2+ (which introduces `HorizontalDivider`, replaces `Divider`)
- `material-icons-extended:1.5.4` - not 1.6+ (which introduces `Icons.AutoMirrored`)
- `lifecycle-viewmodel-compose:2.7.0` - provides `viewModel()` composable for Compose
**Alternatives**: Could upgrade the Kotlin compiler extension, but that would require verifying compatibility with the entire ROS 2 native build chain.
**Limitations**: Cannot use newer Compose APIs (e.g., `HorizontalDivider`, `Icons.AutoMirrored`) until the Kotlin compiler extension is upgraded.
**Issues encountered**: Using newer Compose APIs with older library versions causes `Unresolved reference` errors at compile time, not link time. The error messages are clear but the root cause (version mismatch) is not obvious unless you know which API was introduced in which version.
**References**: Kotlin Compose compiler compatibility map.

### Native Code Directory Restructuring

**Why**: The original flat `src/` directory contained ~40 C++ source files with no clear organization - JNI bridge code, ROS interface, sensor implementations, sensor controllers, camera code, and core utilities were all mixed together at the root level. This made navigation difficult and obscured the architectural layers (JNI boundary, core utilities, ROS integration, device abstractions, ROS publishers).
**Approach**: Reorganized `src/` into a hierarchical structure that separates abstraction layers:

```
src/
├── jni/              # JNI bridge & JVM utilities
│   ├── jni_bridge.cc
│   ├── jvm.cc
│   └── jvm.h
├── core/             # Cross-cutting utilities (log, events, notifications)
│   ├── log.h
│   ├── events.h
│   ├── notification_queue.h
│   ├── network_manager.cc
│   └── network_manager.h
├── ros/              # ROS 2 interface layer
│   ├── ros_interface.cc
│   └── ros_interface.h
├── sensors/          # Sensor subsystem
│   ├── base/         # Abstract interfaces (sensor.h, sensor_descriptor.h, sensor_data_provider.h)
│   ├── impl/         # Concrete implementations (accelerometer, gyroscope, barometer, illuminance, magnetometer, gps)
│   ├── controllers/  # ROS 2 publishers (one controller per sensor type)
│   ├── sensors.cc    # Sensor registry/factory
│   ├── sensors.h
│   ├── sensor_manager.cc
│   └── sensor_manager.h
└── camera/           # Camera subsystem
    ├── base/         # Camera abstractions (camera_descriptor.h, camera_device.h)
    ├── camera_manager.cc
    ├── camera_manager.h
    └── controllers/  # ROS 2 camera publisher (camera_controller.h)
```

Files were moved using `git mv` to preserve history. All `#include` directives (43 files total) were updated to use paths relative to `src/` root. `CMakeLists.txt` was updated with the new paths and grouped by layer (JNI, ROS, sensors base/impl/controllers, camera base/controllers).

**Alternatives**: Could have used a feature-based structure (group all camera files together regardless of layer) rather than layer-based, but that would obscure dependencies - controllers depend on base classes, base classes don't depend on controllers.
**Limitations**: None - the new structure is backward-compatible since all paths are updated.
**Issues encountered**: None significant. The restructuring was mechanical: create directories, move files with `git mv`, update includes, update CMakeLists.txt.
**References**: Clean Architecture principles (dependency hierarchy), sensors_for_ros original structure (flat layout with controllers/ subdirectory).

### Code Cleanup and Manager Pattern Refactoring

**Why**: After multiple feature additions (sensors, cameras, GPS, notifications, callbacks), the `AndroidApp` class in `jni_bridge.cc` grew to 475 lines with mixed responsibilities - managing sensors, cameras, network configuration, JSON serialization, and ROS lifecycle. This "god object" anti-pattern made the code difficult to test, extend, and reason about.

**Approach**: Applied the Manager pattern to extract cohesive subsystems into dedicated manager classes:

1. **SensorManager** (`sensors/sensor_manager.h/cc`):
   - Encapsulates sensor initialization, controller creation, enable/disable logic
   - Moved sensor registry (`Sensors`) and controller lifecycle from AndroidApp
   - Provides clean interface: `Initialize()`, `CreateControllers()`, `EnableSensor()`, `DisableSensor()`
   - Reduced sensor-related code in AndroidApp from ~150 lines to 3 manager method calls

2. **NetworkManager** (`core/network_manager.h/cc`):
   - Manages Cyclone DDS configuration generation with caching
   - Only regenerates `cyclonedds.xml` when domain ID or network interface changes
   - Stores network interface list
   - Reduced network config code from ~80 lines to single `GenerateCycloneDdsConfig()` call

3. **ROS Publisher Observer Lifetime Fixes**:
   - **Problem**: Publishers registered observer callbacks in RosInterface but never removed them, causing use-after-free bugs when publishers were destroyed
   - **Solution**: Changed observer storage from `vector` to `map<ObserverId, callback>` with ID-based tracking
   - Added `RemoveObserver(ObserverId)` method to RosInterface
   - Publisher destructor now automatically removes all registered observers
   - Each `AddObserver()` call returns an `ObserverId` that Publisher tracks in `vector<ObserverId>`

**Code removed**:
- 70+ lines of JSON serialization methods (`GetSensorListJson`, `GetCameraListJson`, etc.)
- Duplicate sensor type-switching logic (now in SensorManager)
- Inline Cyclone DDS XML generation with no caching (now in NetworkManager)

**AndroidApp before/after**:
```cpp
// Before: 475 lines, direct sensor/camera/network management
AndroidApp(const std::string &cache_dir, const std::string &package_name)
    : sensors_(package_name) {
  // 30+ lines of sensor initialization
}

void StartRos(...) {
  // 100+ lines: generate cyclonedds.xml, init ROS, create controllers
}

// After: 250 lines, delegates to managers
AndroidApp(const std::string &cache_dir, const std::string &package_name)
    : sensor_manager_(package_name),
      network_manager_() {
  sensor_manager_.Initialize();
}

void StartRos(...) {
  if (!network_manager_.GenerateCycloneDdsConfig(cache_dir_, ros_domain_id, network_interface)) {
    return;
  }
  ros_.emplace(device_id);
  ros_->Initialize(ros_domain_id);
  size_t sensor_count = sensor_manager_.CreateControllers(*ros_);
}
```

**Benefits**:
- Reduced AndroidApp from 475 to ~250 lines (47% reduction)
- Each manager has single responsibility (testable in isolation)
- No more duplicate type-switching logic
- NetworkManager caching prevents redundant file I/O
- Fixed resource leaks (observer lifetime bug)
- Clearer dependency flow: AndroidApp → Managers → ROS/Sensors/Cameras

**Alternatives**: Could have split AndroidApp into multiple classes without Manager pattern, but managers provide clearer ownership and lifecycle semantics.
**Limitations**: Managers currently have no error recovery beyond logging - failures propagate to AndroidApp as nullptrs/false returns.
**Issues encountered**:
- Initial namespace issue: `SensorDataProvider` in sensor_manager.cc needed `ros2_android::` qualifier
- Observer removal required careful threading (must use mutex, copy observers before invoking outside lock)
**References**: Clean Code (Martin), Manager pattern (Gamma et al.).
