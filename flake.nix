{
  description = "ROS 2 Humble cross-compilation environment for Android";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs, flake-utils, ... }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = import nixpkgs {
          inherit system;
          config = {
            allowUnfree = true;
            allowUnfreePredicate = _: true;
            android_sdk.accept_license = true;
          };
        };

        # Android SDK per exposee: Android 13 / API 33, NDK 25.1, arm64-v8a
        androidComposition = pkgs.androidenv.composeAndroidPackages {
          cmdLineToolsVersion = "8.0";
          platformToolsVersion = "35.0.2";
          buildToolsVersions = [ "33.0.2" "34.0.0" ];
          platformVersions = [ "33" "34" ];
          includeEmulator = false;
          includeSystemImages = false;
          includeSources = false;
          includeNDK = true;
          ndkVersions = [ "25.1.8937393" ];
          useGoogleAPIs = false;
          cmakeVersions = [ "3.22.1" ];
        };

        androidSdk = androidComposition.androidsdk;

        # ROS 2 Humble requires empy 3.x - nixpkgs ships empy 4.x which is incompatible
        empy3 = pkgs.python3.pkgs.buildPythonPackage rec {
          pname = "empy";
          version = "3.3.4";
          format = "setuptools";
          src = pkgs.python3.pkgs.fetchPypi {
            inherit pname version;
            sha256 = "c6xJeFtgFHnfTqGKfHm8EwSop8NMArlHLPEgauiPAbM=";
          };
          doCheck = false;
        };

        pythonEnv = pkgs.python3.withPackages
          (ps: with ps; [ catkin-pkg empy3 lark pip setuptools ]);

        buildDeps = with pkgs; [
          # Android SDK (includes platform-tools/adb, build-tools, NDK, cmake)
          androidSdk

          # Python with ROS 2 build dependencies
          pythonEnv

          # JDK for keytool (debug keystore), apksigner, and Gradle
          jdk17

          # Gradle for Kotlin/APK build
          gradle

          # Build tools (cmake comes from Android SDK at 3.22.1)
          gnumake
          zip
          unzip
          git

          # ROS 2 workspace tools
          vcstool
        ];

      in {
        packages.default = pkgs.buildEnv {
          name = "ros2-android-env";
          paths = buildDeps;
        };

        devShells.default = pkgs.mkShell {
          name = "ros2-android-shell";
          buildInputs = buildDeps;

          ANDROID_HOME = "${androidSdk}/libexec/android-sdk";
          ANDROID_SDK_ROOT = "${androidSdk}/libexec/android-sdk";

          JAVA_HOME = "${pkgs.jdk17}";

          shellHook = ''
            # Use Android SDK's CMake 3.22.1 (CMake 4.x breaks old cmake_minimum_required)
            export PATH="${androidSdk}/libexec/android-sdk/cmake/3.22.1/bin:$PATH"

            # Always regenerate local.properties (SDK path changes with each Nix rebuild)
            echo "sdk.dir=$ANDROID_HOME" > local.properties

            echo "ROS 2 Android build environment"
            echo "  CMake: $(cmake --version | head -1)"
            echo "  NDK: 25.1.8937393 (arm64-v8a)"
            echo "  Platform: API 33 (Android 13)"
            echo "  Build Tools: 33.0.2"
            echo "  ANDROID_HOME: $ANDROID_HOME"
          '';
        };
      });
}
