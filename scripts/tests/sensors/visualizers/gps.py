"""GPS sensor visualizer using Folium interactive maps"""

import rclpy
from rclpy.node import Node
from sensor_msgs.msg import NavSatFix
import folium
import folium.plugins
import webbrowser
import tempfile
import os
import numpy as np


class GPSVisualizer:
    """Interactive map visualization with GPS location and accuracy circle"""

    def __init__(self, topic_name):
        self.topic_name = topic_name

        # Initialize ROS 2
        if not rclpy.ok():
            rclpy.init()
        self.node = Node('gps_visualizer')

        # State
        self.html_path = os.path.join(tempfile.gettempdir(), 'gps_location.html')
        self.browser_opened = False
        self.update_count = 0
        self.last_lat = None
        self.last_lon = None

        # Subscribe to topic
        self.subscription = self.node.create_subscription(
            NavSatFix,
            topic_name,
            self.callback,
            10
        )

    def callback(self, msg):
        """Process incoming GPS messages"""
        # Check for valid GPS fix
        if msg.latitude == 0.0 and msg.longitude == 0.0:
            if self.update_count % 10 == 0:
                print("⏳ No GPS fix yet... (Move device outdoors for better signal)")
            self.update_count += 1
            return

        # Calculate accuracy from covariance
        accuracy = np.sqrt(msg.position_covariance[0]) if msg.position_covariance[0] > 0 else 999.0

        # Store current position
        self.last_lat = msg.latitude
        self.last_lon = msg.longitude

        # Create map centered on GPS position
        m = folium.Map(
            location=[msg.latitude, msg.longitude],
            zoom_start=18,
            tiles="OpenStreetMap"
        )

        # Add accuracy circle
        folium.Circle(
            [msg.latitude, msg.longitude],
            radius=accuracy,
            color='blue',
            fill=True,
            fillColor='blue',
            fillOpacity=0.2,
            popup=f'Accuracy: ±{accuracy:.1f}m',
            tooltip=f'Uncertainty radius: {accuracy:.1f}m'
        ).add_to(m)

        # Add position marker
        status_icon = "ok-sign" if accuracy < 20 else "warning-sign"
        status_color = "green" if accuracy < 20 else "orange" if accuracy < 50 else "red"

        # GPS status mapping
        status_map = {
            -1: "No Fix",
            0: "Standard GPS",
            1: "Satellite-Based Augmentation",
            2: "Ground-Based Augmentation"
        }
        gps_status = status_map.get(msg.status.status, "Unknown")

        folium.Marker(
            [msg.latitude, msg.longitude],
            popup=f"""
            <b>GPS Position</b><br><br>
            <b>Coordinates:</b><br>
            Latitude: {msg.latitude:.7f}°<br>
            Longitude: {msg.longitude:.7f}°<br>
            Altitude: {msg.altitude:.1f} m<br><br>
            <b>Accuracy:</b> ±{accuracy:.1f} m<br>
            <b>Status:</b> {gps_status}<br>
            <b>Updates:</b> {self.update_count}
            """,
            tooltip="📍 Current Position",
            icon=folium.Icon(color=status_color, icon=status_icon)
        ).add_to(m)

        # Add measurement control for distances
        folium.plugins.MeasureControl(
            position='topleft',
            primary_length_unit='meters',
            secondary_length_unit='kilometers'
        ).add_to(m)

        # Add fullscreen control
        folium.plugins.Fullscreen(position='topleft').add_to(m)

        # Save map
        m.save(self.html_path)

        # Open browser on first update
        if not self.browser_opened:
            webbrowser.open(f'file://{self.html_path}')
            self.browser_opened = True
            print(f"\n✓ Map opened in browser: {self.html_path}")
            print("   (The map will auto-refresh as you receive GPS updates)\n")

        self.update_count += 1

        # Print update
        quality = (
            "EXCELLENT" if accuracy < 5 else
            "GOOD" if accuracy < 10 else
            "FAIR" if accuracy < 20 else
            "POOR"
        )

        print(f"[{self.update_count:4d}] "
              f"Lat: {msg.latitude:10.6f}°  "
              f"Lon: {msg.longitude:10.6f}°  "
              f"Alt: {msg.altitude:6.1f}m  "
              f"Acc: ±{accuracy:5.1f}m  "
              f"[{quality}]")

        if self.update_count % 10 == 0:
            print(f"      💾 Map refreshed ({self.update_count} updates)")

    def run(self):
        """Start the visualization"""
        print("\nGPS Visualization")
        print("-" * 70)
        print("Test procedure:")
        print("  1. Go outdoors for better GPS fix")
        print("  2. Wait for accuracy < 20m (green marker)")
        print("  3. Walk around → marker updates on map")
        print("  4. Check accuracy circle matches position uncertainty")
        print("\nMap features:")
        print("  - Blue circle: position uncertainty radius")
        print("  - Green marker: good accuracy (< 20m)")
        print("  - Orange marker: fair accuracy (20-50m)")
        print("  - Red marker: poor accuracy (> 50m)")
        print("\nPress Ctrl+C to exit\n")
        print("="*70)
        print("Waiting for GPS data...\n")

        try:
            rclpy.spin(self.node)
        except KeyboardInterrupt:
            print(f"\n\n{'='*70}")
            print(f"✓ GPS test completed ({self.update_count} position updates)")
            if self.last_lat and self.last_lon:
                print(f"  Final position: {self.last_lat:.6f}°, {self.last_lon:.6f}°")
            print(f"{'='*70}\n")

        # Cleanup
        self.node.destroy_node()
