"""Camera visualizer using rqt_image_view"""

import subprocess
import sys


class CameraVisualizer:
    """Launch rqt_image_view for camera feed visualization"""

    def __init__(self, topic_name):
        self.topic_name = topic_name

    def run(self):
        """Launch rqt_image_view"""
        print("\nCamera Visualization")
        print("-" * 70)
        print(f"Topic: {self.topic_name}")
        print("\nLaunching rqt_image_view...")
        print("\nClose the rqt_image_view window to return to the menu.\n")
        print("="*70 + "\n")

        try:
            # Launch rqt_image_view with the topic
            result = subprocess.run(
                ['ros2', 'run', 'rqt_image_view', 'rqt_image_view', self.topic_name],
                check=False
            )

            if result.returncode != 0:
                print(f"\n⚠ rqt_image_view exited with code {result.returncode}")
                print("Make sure rqt_image_view is installed:")
                print("  sudo apt install ros-humble-rqt-image-view")

        except FileNotFoundError:
            print("\n✗ Error: rqt_image_view not found!")
            print("\nPlease install it with:")
            print("  sudo apt install ros-humble-rqt-image-view")
            sys.exit(1)
        except KeyboardInterrupt:
            print("\n\n✓ Camera visualization stopped")
