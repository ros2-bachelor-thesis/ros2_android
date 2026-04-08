# ROS 2 Android Build System
# Two-stage build: CMake cross-compilation + Gradle APK

SHELL := bash
.DEFAULT_GOAL := all

# Directories
BUILD_DIR := build
DEPS_DIR := deps
JNI_LIBS_DIR := $(BUILD_DIR)/jniLibs/arm64-v8a

# Build type: RelWithDebInfo (default), Debug, or Release
BUILD_TYPE ?= RelWithDebInfo

# APK build variant (debug/release)
ifeq ($(BUILD_TYPE),Release)
  GRADLE_TASK := assembleRelease
  APK_VARIANT := release
  APK_OUTPUT := app/build/outputs/apk/release/app-release.apk
else
  GRADLE_TASK := assembleDebug
  APK_VARIANT := debug
  APK_OUTPUT := app/build/outputs/apk/debug/app-debug.apk
endif

# Convenience aliases
.PHONY: debug release
debug:
	$(MAKE) all BUILD_TYPE=Debug
release:
	$(MAKE) all BUILD_TYPE=Release

# Parallelism
NPROC := $(shell nproc)

# Marker files for incremental builds
DEPS_STAMP := $(DEPS_DIR)/.deps-fetched
NATIVE_STAMP := $(BUILD_DIR)/.native-built-$(BUILD_TYPE)
APK_STAMP := app/build/.apk-built-$(APK_VARIANT)

.PHONY: all native app clean clean-app clean-native clean-deps help \
       setup-install install run logcat

# ============================================================================
# Main targets
# ============================================================================

## Build everything (deps + native + app)
all: native app

## Fetch git submodules and ROS 2 source dependencies
deps: $(DEPS_STAMP)

$(DEPS_STAMP): ros.repos
	@echo "==> Initializing git submodules..."
	git submodule init
	git submodule update
	@echo "==> Fetching ROS 2 dependencies via vcs..."
	vcs import --input ros.repos $(DEPS_DIR)/
	@echo "==> Applying Android patches for YDLidar SDK..."
	cd $(DEPS_DIR)/ydlidar_sdk && patch -p1 < $(CURDIR)/android_patches/ydlidar_sdk_android_support.patch || true
	@echo "==> Fetching ros2_android_perception dependencies..."
	cd $(DEPS_DIR)/ros2_android_perception && $(MAKE) deps
	@touch $(DEPS_STAMP)
	@echo "==> Dependencies fetched"

## Build native ROS 2 dependencies (CMake cross-compilation)
native: $(NATIVE_STAMP)

NATIVE_SOURCES := $(shell find src -type f \( -name '*.cc' -o -name '*.h' -o -name '*.cpp' -o -name 'CMakeLists.txt' \) 2>/dev/null)

$(NATIVE_STAMP): $(DEPS_STAMP) CMakeLists.txt dependencies.cmake dep_build.cmake $(NATIVE_SOURCES)
	@echo "==> Building native ROS 2 dependencies..."
	@mkdir -p $(BUILD_DIR)
	cd $(BUILD_DIR) && cmake ../ -DANDROID_HOME=$(ANDROID_HOME) -DCMAKE_BUILD_TYPE=$(BUILD_TYPE)
	cd $(BUILD_DIR) && $(MAKE) -j$(NPROC)
	@touch $(NATIVE_STAMP)
	@echo "==> Native build complete"

## Build Android APK (requires native deps)
app: $(APK_STAMP)

$(APK_STAMP): $(NATIVE_STAMP) $(shell find app/src -type f 2>/dev/null) app/build.gradle.kts
	@echo "==> Building Android APK ($(APK_VARIANT))..."
	./gradlew :app:$(GRADLE_TASK)
	@touch $(APK_STAMP)
	@echo "==> APK built: $(APK_OUTPUT)"

# ============================================================================
# Utility targets
# ============================================================================

## Verify a device is connected and ready for debugging
setup-install:
	@echo "==> Checking for connected Android device..."
	@adb devices | grep -q 'device$$' || \
		(echo "ERROR: No device found. Enable USB debugging and connect your phone." && exit 1)
	@echo "==> Device found:"
	@adb devices -l | grep 'device ' | head -1
	@echo "==> Android version: $$(adb shell getprop ro.build.version.release)"
	@echo "==> ABI: $$(adb shell getprop ro.product.cpu.abi)"
	@echo "==> Device ready for deployment"

## Install APK to connected device
install: setup-install $(APK_STAMP)
	@echo "==> Installing APK to device..."
	adb install -r $(APK_OUTPUT)
	@echo "==> Installed successfully"

## Build, install, and launch app on device
run: install
	@echo "==> Launching app..."
	adb shell am start -n com.github.mowerick.ros2.android/.MainActivity
	@echo "==> App launched"

## Tail device logs filtered to the app
logcat:
	adb logcat -c && adb logcat -v color --pid=$$(adb shell pidof com.github.mowerick.ros2.android) *:D 2>/dev/null

## Clean everything
clean: clean-native clean-app

## Clean only the app build (keeps native deps)
clean-app:
	@echo "==> Cleaning app build..."
	./gradlew clean || true
	rm -f $(APK_STAMP)

## Clean native build (full rebuild required after this)
clean-native:
	@echo "==> Cleaning native build..."
	rm -rf $(BUILD_DIR)

## Clean fetched dependencies (re-fetch required after this)
clean-deps:
	@echo "==> Cleaning fetched dependencies..."
	rm -f $(DEPS_STAMP)
	find $(DEPS_DIR) -mindepth 1 ! -name 'COLCON_IGNORE' -exec rm -rf {} + 2>/dev/null || true

## Show help
help:
	@echo "ROS 2 Android Build System"
	@echo ""
	@echo "Usage: make [target]"
	@echo ""
	@echo "Targets:"
	@echo "  all          Build everything, RelWithDebInfo [default]"
	@echo "  debug        Build everything with debug symbols (no optimization)"
	@echo "  release      Build everything optimized (no debug symbols)"
	@echo "  deps         Fetch git submodules + ROS 2 source deps"
	@echo "  native       Build native ROS 2 dependencies only"
	@echo "  app          Build Android APK only (requires native)"
	@echo "  setup-install  Verify device is connected and ready"
	@echo "  install      Build + install APK to connected device"
	@echo "  run          Build, install, and launch app on device"
	@echo "  logcat       Tail device logs filtered to the app"
	@echo "  clean        Clean build artifacts (runs clean-native and clean-app)"
	@echo "  clean-app    Clean app build only(keeps native)"
	@echo "  clean-native Clean native build (full rebuild needed)"
	@echo "  clean-deps   Clean fetched dependencies"
	@echo "  help         Show this help"
