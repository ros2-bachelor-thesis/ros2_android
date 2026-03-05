# ROS 2 Android Build System
# Two-stage build: CMake cross-compilation + Gradle APK

SHELL := bash
.DEFAULT_GOAL := all

# Directories
BUILD_DIR := build
DEPS_DIR := deps
JNI_LIBS_DIR := $(BUILD_DIR)/jniLibs/arm64-v8a
APK_OUTPUT := app/build/outputs/apk/debug/app-debug.apk

# Parallelism
NPROC := $(shell nproc)

# Marker files for incremental builds
DEPS_STAMP := $(DEPS_DIR)/.deps-fetched
NATIVE_STAMP := $(BUILD_DIR)/.native-built
APK_STAMP := app/build/.apk-built

.PHONY: all native app clean clean-app clean-native clean-deps help install

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
	@touch $(DEPS_STAMP)
	@echo "==> Dependencies fetched"

## Build native ROS 2 dependencies (CMake cross-compilation)
native: $(NATIVE_STAMP)

$(NATIVE_STAMP): $(DEPS_STAMP) CMakeLists.txt dependencies.cmake dep_build.cmake
	@echo "==> Building native ROS 2 dependencies..."
	@mkdir -p $(BUILD_DIR)
	cd $(BUILD_DIR) && cmake ../ -DANDROID_HOME=$(ANDROID_HOME)
	cd $(BUILD_DIR) && $(MAKE) -j$(NPROC)
	@touch $(NATIVE_STAMP)
	@echo "==> Native build complete"

## Build Android APK (requires native deps)
app: $(APK_STAMP)

$(APK_STAMP): $(NATIVE_STAMP) $(shell find app/src -type f 2>/dev/null) app/build.gradle.kts
	@echo "==> Building Android APK..."
	./gradlew :app:assembleDebug
	@touch $(APK_STAMP)
	@echo "==> APK built: $(APK_OUTPUT)"

# ============================================================================
# Utility targets
# ============================================================================

## Install APK to connected device
install: $(APK_STAMP)
	@echo "==> Installing APK to device..."
	adb install -r $(APK_OUTPUT)

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
	@echo "  all          Build everything (deps + native + app) [default]"
	@echo "  deps         Fetch git submodules + ROS 2 source deps"
	@echo "  native       Build native ROS 2 dependencies only"
	@echo "  app          Build Android APK only (requires native)"
	@echo "  install      Install APK to connected device"
	@echo "  clean        Clean build artifacts (keeps deps)"
	@echo "  clean-app    Clean app build only (keeps native)"
	@echo "  clean-native Clean native build (full rebuild needed)"
	@echo "  clean-deps   Clean fetched dependencies"
	@echo "  help         Show this help"
