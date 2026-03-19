# ROS 2 Android Sensor Testing Framework

Interactive Python tool for visualizing and testing sensor data published by the ros2_android application.

> [!NOTE]
> If you have troubles discovering topics, use the [Network setup script](../setup_ros2_network.sh) to setup your network.

## Features

- **Auto-discovery**: Automatically finds available sensor topics from the Android app
- **Interactive menu**: Select which sensor to test from a user-friendly CLI
- **Browser-based visualization**: Real-time plots render in your web browser (WebAgg backend)
- **Custom visualizations**: Tailored visualization for each sensor type
- **Validation**: Built-in validation checks for expected sensor ranges
- **Automatic cleanup**: Properly manages visualization state between sensor tests

## Supported Sensors

### IMU Sensors

- **Accelerometer** (`/<device_id>/sensors/accelerometer`) - 3D acceleration with gravity validation
- **Gyroscope** (`/<device_id>/sensors/gyroscope`) - Angular velocity with rotation detection
- **Magnetometer** (`/<device_id>/sensors/magnetometer`) - Compass with magnetic field visualization

### Environmental Sensors

- **Barometer** (`/<device_id>/sensors/barometer`) - Pressure and altitude estimation
- **Illuminance** (`/<device_id>/sensors/illuminance`) - Light sensor with animated brightness circle

### Positioning

- **GPS** (`/<device_id>/sensors/gps`) - Interactive Folium map with accuracy circle

### Camera

- **Camera Image** (`/<device_id>/camera/<id>/image_color`) - Raw/compressed image via rqt_image_view
- **Camera Info** (`/<device_id>/camera/<id>/camera_info`) - Calibration data display

> [!NOTE]
> `<device_id>` is the device identifier configured in the ros2_android app (e.g., `pixel_7`). Replace it with your actual device ID when running tests.

## Prerequisites

### 1. ROS 2 Humble

Make sure ROS 2 Humble is installed:

```bash
# Check if ROS 2 is installed
ros2 --version

# If not installed, install ROS 2 Humble Desktop
sudo apt update
sudo apt install ros-humble-desktop
```

### 2. Source ROS 2 Environment

```bash
source /opt/ros/humble/setup.bash
```

Add this to your `~/.bashrc` to make it permanent:

```bash
echo "source /opt/ros/humble/setup.bash" >> ~/.bashrc
```

### 3. Install rqt_image_view (for camera visualization)

```bash
sudo apt install ros-humble-rqt-image-view
```

## Installation

### 1. Navigate to the test scripts directory

```bash
cd scripts/tests
```

### 2. Install Python dependencies

```bash
pip install -r requirements.txt
```

Or using a virtual environment (recommended):

```bash
# Create virtual environment
python3 -m venv venv

# Activate it
source venv/bin/activate

# Install dependencies
pip install -r requirements.txt
```

> [!NOTE]
> If using a virtual environment, you need to activate it every time before running the script.

### 3. Make the script executable

```bash
chmod +x sensor_test.py
```

## Usage

### 1. Start the ros2_android Application

Make sure your Android device is running the ros2_android app and is connected to the same network as your testing machine. The app should be publishing sensor data.

### 2. Verify ROS 2 connectivity

Check that you can see the sensor topics:

```bash
ros2 topic list
```

You should see topics like `/<device_id>/sensors/accelerometer`, `/<device_id>/sensors/gps`, etc. (where `<device_id>` is your configured device identifier).

### 3. Run the Testing Framework

```bash
./sensor_test.py
```

Or:

```bash
python3 sensor_test.py
```

### 4. Interactive Menu

The script will:

1. Auto-discover available sensor topics
2. Display an interactive menu
3. Allow you to select which sensor to visualize
4. Launch the appropriate visualization tool
5. Return to the menu when you close the visualization

### 5. Navigation

- Use **arrow keys** to navigate the menu
- Press **Enter** to select
- Press **Ctrl+C** to exit at any time
- Close visualization windows to return to the menu

> [!NOTE]
> Most visualizations open in your default web browser (WebAgg backend). Close the browser tab to return to the menu. The camera visualization uses a separate rqt_image_view window.
