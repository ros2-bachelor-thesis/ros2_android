#!/usr/bin/env python3
"""
ROS 2 Android Sensor Testing Script

Interactive tool for visualizing and testing sensor data published by the ros2_android app.
Automatically discovers available sensor topics and launches appropriate visualizations.
"""

import sys
import os
import rclpy
from rclpy.node import Node
import questionary
from questionary import Style, Separator

# Import visualization modules
from visualizers import (
    AccelerometerVisualizer,
    GyroscopeVisualizer,
    MagnetometerVisualizer,
    BarometerVisualizer,
    IlluminanceVisualizer,
    GPSVisualizer,
    CameraVisualizer
)


# Custom style for the CLI
custom_style = Style([
    ('qmark', 'fg:#673ab7 bold'),
    ('question', 'bold'),
    ('answer', 'fg:#2196f3 bold'),
    ('pointer', 'fg:#673ab7 bold'),
    ('highlighted', 'fg:#673ab7 bold'),
    ('selected', 'fg:#4caf50'),
    ('separator', 'fg:#cc5454'),
    ('instruction', ''),
])


class SensorTopicDiscovery:
    """Discovers and categorizes available sensor topics"""

    def __init__(self):
        rclpy.init()
        self.node = Node('sensor_topic_discovery')
        # Give the node a moment to discover existing topics
        import time
        time.sleep(0.5)

    def get_available_topics(self):
        """Get all available sensor and camera topics"""
        all_topics = self.node.get_topic_names_and_types()

        # Filter to sensor and camera topics
        # Topics now have device_id prefix: /<device_id>/sensors/... or /<device_id>/camera/...
        sensor_topics = []
        for topic_name, topic_types in all_topics:
            # Match topics with pattern: /<something>/sensors/... or /<something>/camera/...
            if '/sensors/' in topic_name or '/camera/' in topic_name:
                sensor_topics.append({
                    'name': topic_name,
                    'type': topic_types[0] if topic_types else 'unknown',
                    'category': self._categorize_topic(topic_name, topic_types[0] if topic_types else '')
                })

        return sensor_topics

    def _categorize_topic(self, name, msg_type):
        """Categorize topic based on name and message type"""
        if '/accelerometer' in name:
            return 'accelerometer'
        elif '/gyroscope' in name:
            return 'gyroscope'
        elif '/magnetometer' in name:
            return 'magnetometer'
        elif '/barometer' in name:
            return 'barometer'
        elif '/illuminance' in name:
            return 'illuminance'
        elif '/gps' in name:
            return 'gps'
        elif '/camera/' in name:
            if '/image_color' in name and 'compressed' not in name:
                return 'camera_image'
            elif '/compressed' in name:
                return 'camera_compressed'
            elif '/camera_info' in name:
                return 'camera_info'
        return 'unknown'

    def cleanup(self):
        """Cleanup ROS 2 resources"""
        self.node.destroy_node()
        rclpy.shutdown()


def clear_screen():
    """Clear the terminal screen"""
    os.system('clear' if os.name == 'posix' else 'cls')


def display_welcome():
    """Display welcome banner"""
    clear_screen()
    print("\n" + "="*70)
    print("  ROS 2 Android Sensor Testing Framework")
    print("  Interactive Visualization and Validation Tool")
    print("="*70)
    print("\nThis tool helps you test and visualize sensor data from the Android app.")
    print("Make sure the ros2_android app is running and publishing sensor data.\n")


def format_topic_choice(topic):
    """Format topic for display in menu"""
    category_labels = {
        'accelerometer': '[IMU] Accelerometer',
        'gyroscope': '[IMU] Gyroscope',
        'magnetometer': '[IMU] Magnetometer',
        'barometer': '[ENV] Barometer',
        'illuminance': '[ENV] Light Sensor',
        'gps': '[LOC] GPS',
        'camera_image': '[CAM] Image (Raw)',
        'camera_compressed': '[CAM] Image (Compressed)',
        'camera_info': '[CAM] Camera Info'
    }

    label = category_labels.get(topic['category'], '[???]')
    return f"{label:30s} {topic['name']}"


def launch_visualizer(topic):
    """Launch appropriate visualizer for the selected topic"""
    category = topic['category']
    topic_name = topic['name']

    # Clean up any previous visualization state before starting new one
    from visualizers.matplotlib_config import cleanup_visualization
    cleanup_visualization()

    print(f"\n{'='*70}")
    print(f"Starting visualization: {topic_name}")
    print(f"{'='*70}\n")

    try:
        if category == 'accelerometer':
            visualizer = AccelerometerVisualizer(topic_name)
            visualizer.run()
        elif category == 'gyroscope':
            visualizer = GyroscopeVisualizer(topic_name)
            visualizer.run()
        elif category == 'magnetometer':
            visualizer = MagnetometerVisualizer(topic_name)
            visualizer.run()
        elif category == 'barometer':
            visualizer = BarometerVisualizer(topic_name)
            visualizer.run()
        elif category == 'illuminance':
            visualizer = IlluminanceVisualizer(topic_name)
            visualizer.run()
        elif category == 'gps':
            visualizer = GPSVisualizer(topic_name)
            visualizer.run()
        elif category in ['camera_image', 'camera_compressed']:
            visualizer = CameraVisualizer(topic_name)
            visualizer.run()
        elif category == 'camera_info':
            # Camera info uses subprocess, handle separately
            import subprocess
            subprocess.run(['ros2', 'topic', 'echo', topic_name, '--once'])
        else:
            print(f"⚠ No visualizer available for topic type: {category}")
            print(f"Using ros2 topic echo instead...\n")
            import subprocess
            subprocess.run(['ros2', 'topic', 'echo', topic_name])

    except KeyboardInterrupt:
        print("\n\n✓ Visualization stopped by user")
    except Exception as e:
        print(f"\n✗ Error running visualizer: {e}")
        import traceback
        traceback.print_exc()
    finally:
        # Ensure cleanup happens even if there's an error
        from visualizers.matplotlib_config import cleanup_visualization
        cleanup_visualization()


def main():
    """Main interactive loop"""
    display_welcome()

    # Discover available topics
    print("🔍 Discovering available sensor topics...")
    discovery = SensorTopicDiscovery()

    try:
        retry_count = 0
        max_retries = 5

        while True:
            # Try scanning twice before showing retry window
            topics = discovery.get_available_topics()
            if not topics:
                import time
                time.sleep(0.5)
                topics = discovery.get_available_topics()

            if not topics:
                retry_count += 1

                if retry_count == 1:
                    print(f"\n⚠ No sensor topics found! (Attempt {retry_count}/{max_retries})")
                    print("Make sure the ros2_android app is running and publishing data.")
                    print("\nRetrying every 3 seconds...")
                else:
                    # Move up 4 lines to the counter line, clear it and rewrite
                    print(f"\033[4A\033[4K⚠ No sensor topics found! (Attempt {retry_count}/{max_retries})")
                    # Move down 1 line to restore cursor position
                    print("\033[3B", end='', flush=True)

                import time
                time.sleep(3)

                # After max retries, show detailed error
                if retry_count >= max_retries:
                    print("\n❌ Failed to discover topics after 5 attempts!")
                    print("\nPlease check:")
                    print("  • Network connectivity (Wi-Fi/multicast enabled)")
                    print("  • ROS 2 configuration (ROS_DOMAIN_ID matches)")
                    print("  • ros2_android app is running and publishing data")
                    print("  • No firewall blocking DDS traffic")

                    retry = questionary.confirm(
                        "\nWould you like to keep trying?",
                        default=True,
                        style=custom_style
                    ).ask()

                    if not retry:
                        print("\n👋 Exiting. Goodbye!")
                        break

                    retry_count = 0
                    display_welcome()
                    print("🔍 Discovering available sensor topics...")

                continue

            # Reset retry count on success
            retry_count = 0

            print(f"\n✓ Found {len(topics)} sensor topic(s)\n")

            # Create menu choices
            choices = [format_topic_choice(t) for t in topics]
            choices.append(Separator())
            choices.append("🔄 Refresh topic list")
            choices.append("❌ Exit")

            # Show interactive menu
            selection = questionary.select(
                "Select a sensor topic to visualize:",
                choices=choices,
                style=custom_style
            ).ask()

            if selection is None or "❌ Exit" in selection:
                print("\n👋 Exiting sensor test framework. Goodbye!")
                break

            if "🔄 Refresh" in selection:
                clear_screen()
                print("\n🔄 Refreshing topic list...")
                continue

            # Find the selected topic
            # Sort topics by name length (descending) to match longest first
            # This prevents substring matches (e.g., /camera/1/image_color matching
            # when user selected /camera/1/image_color/compressed)
            selected_topic = None
            for topic in sorted(topics, key=lambda t: len(t['name']), reverse=True):
                if topic['name'] in selection:
                    selected_topic = topic
                    break

            if selected_topic:
                launch_visualizer(selected_topic)
                print("\n" + "="*70)
                print("Returning to main menu...")
                print("="*70)
                input("Press Enter to continue...")
                clear_screen()

    finally:
        discovery.cleanup()


if __name__ == '__main__':
    try:
        main()
    except KeyboardInterrupt:
        print("\n\n👋 Exiting. Goodbye!")
        sys.exit(0)
