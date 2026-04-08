"""Barometer sensor visualizer"""

import rclpy
from rclpy.node import Node
from sensor_msgs.msg import FluidPressure
from .matplotlib_config import configure_matplotlib, cleanup_visualization
import matplotlib.pyplot as plt
configure_matplotlib()
from matplotlib.animation import FuncAnimation
from collections import deque


class BarometerVisualizer:
    """Pressure and altitude visualization"""

    def __init__(self, topic_name):
        self.topic_name = topic_name
        self.max_points = 200

        # Initialize ROS 2
        if not rclpy.ok():
            rclpy.init()
        self.node = Node('barometer_visualizer')

        # Data storage
        self.times = deque(maxlen=self.max_points)
        self.pressure_data = deque(maxlen=self.max_points)
        self.altitude_data = deque(maxlen=self.max_points)

        # Create figure
        self.fig, (self.ax1, self.ax2) = plt.subplots(2, 1, figsize=(12, 8))
        self.fig.suptitle(f'Barometer: {topic_name}', fontsize=14, fontweight='bold')

        # Pressure plot
        self.line_p, = self.ax1.plot([], [], 'b-', linewidth=2)
        self.ax1.set_ylabel('Pressure (hPa)', fontsize=12)
        self.ax1.grid(True, alpha=0.3)
        self.ax1.set_title('Atmospheric Pressure', fontsize=12, fontweight='bold')

        # Altitude plot
        self.line_alt, = self.ax2.plot([], [], 'g-', linewidth=2)
        self.ax2.set_ylabel('Altitude (m)', fontsize=12)
        self.ax2.set_xlabel('Sample', fontsize=12)
        self.ax2.grid(True, alpha=0.3)
        self.ax2.set_title('Estimated Altitude (from pressure)', fontsize=12, fontweight='bold')

        # Info text box
        self.info_text = self.ax1.text(
            0.02, 0.95, '', transform=self.ax1.transAxes,
            fontsize=11, verticalalignment='top',
            bbox=dict(boxstyle='round', facecolor='wheat', alpha=0.8)
        )

        # Subscribe to topic
        self.subscription = self.node.create_subscription(
            FluidPressure,
            topic_name,
            self.callback,
            10
        )

    def callback(self, msg):
        """Process incoming barometer messages"""
        pressure_pa = msg.fluid_pressure
        pressure_hpa = pressure_pa / 100.0

        # Calculate altitude using barometric formula
        # h = 44330 * (1 - (P/P0)^0.1903) where P0 = 101325 Pa (sea level)
        altitude = 44330 * (1 - (pressure_pa / 101325.0) ** 0.1903)

        # Store data
        self.times.append(len(self.times))
        self.pressure_data.append(pressure_hpa)
        self.altitude_data.append(altitude)

    def update_plot(self, frame):
        """Update plot with new data"""
        # Spin ROS 2 node to process callbacks
        rclpy.spin_once(self.node, timeout_sec=0)

        if len(self.times) == 0:
            return self.line_p, self.line_alt, self.info_text

        # Update pressure plot
        self.line_p.set_data(self.times, self.pressure_data)

        # Update altitude plot
        self.line_alt.set_data(self.times, self.altitude_data)

        # Update x-axis limits
        if len(self.times) > self.max_points:
            x_min = len(self.times) - self.max_points
            x_max = len(self.times) + 10
        else:
            x_min = 0
            x_max = max(self.max_points, len(self.times) + 10)

        self.ax1.set_xlim(x_min, x_max)
        self.ax2.set_xlim(x_min, x_max)

        # Update y-axis limits with some margin
        if len(self.pressure_data) > 0:
            p_min, p_max = min(self.pressure_data), max(self.pressure_data)
            p_margin = max((p_max - p_min) * 0.1, 0.5)
            self.ax1.set_ylim(p_min - p_margin, p_max + p_margin)

            alt_min, alt_max = min(self.altitude_data), max(self.altitude_data)
            alt_margin = max((alt_max - alt_min) * 0.1, 1)
            self.ax2.set_ylim(alt_min - alt_margin, alt_max + alt_margin)

            # Update info text
            current_pressure = self.pressure_data[-1]
            current_altitude = self.altitude_data[-1]

            # Validate pressure range (typical: 950-1050 hPa at sea level)
            if 950 <= current_pressure <= 1050:
                status = "✓ Normal range"
            else:
                status = "⚠ Outside typical range"

            info = f'Pressure: {current_pressure:.2f} hPa {status}\n'
            info += f'Altitude: {current_altitude:.1f} m (estimated)\n'
            info += f'Note: ~12 Pa change per meter'

            self.info_text.set_text(info)

        return self.line_p, self.line_alt, self.info_text

    def run(self):
        """Start the visualization"""
        print("\nBarometer Visualization")
        print("-" * 50)
        print("Test procedure:")
        print("  1. Note baseline pressure at current height")
        print("  2. Move phone up/down stairs → pressure changes ~12 Pa/meter")
        print("  3. Verify altitude estimation is reasonable")
        print("  4. Typical sea level: 950-1050 hPa")
        print("\nPress Ctrl+C to return to Main menu\n")

        ani = FuncAnimation(self.fig, self.update_plot, interval=100, blit=False, cache_frame_data=False)
        plt.tight_layout()
        plt.show(block=True)

        # Cleanup
        cleanup_visualization()
        self.node.destroy_node()
