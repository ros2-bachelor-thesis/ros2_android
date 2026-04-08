"""Magnetometer sensor visualizer with compass"""

import rclpy
from rclpy.node import Node
from sensor_msgs.msg import MagneticField
from .matplotlib_config import configure_matplotlib, cleanup_visualization
import matplotlib.pyplot as plt
configure_matplotlib()
from matplotlib.animation import FuncAnimation
from matplotlib.patches import Circle, FancyArrowPatch
from collections import deque
import numpy as np


class MagnetometerVisualizer:
    """Compass visualization with magnetic field vector plot"""

    def __init__(self, topic_name):
        self.topic_name = topic_name
        self.max_points = 100

        # Initialize ROS 2
        if not rclpy.ok():
            rclpy.init()
        self.node = Node('magnetometer_visualizer')

        # Data storage
        self.times = deque(maxlen=self.max_points)
        self.x_data = deque(maxlen=self.max_points)
        self.y_data = deque(maxlen=self.max_points)
        self.z_data = deque(maxlen=self.max_points)
        self.current_heading = 0
        self.current_magnitude = 0

        # Create figure
        self.fig = plt.figure(figsize=(14, 6))
        self.fig.suptitle(f'Magnetometer: {topic_name}', fontsize=14, fontweight='bold')

        # Compass display (left)
        self.ax1 = self.fig.add_subplot(121)
        compass_circle = Circle((0, 0), 1, fill=False, edgecolor='black', linewidth=2)
        self.ax1.add_patch(compass_circle)

        self.arrow = FancyArrowPatch((0, 0), (0, 1),
                                     arrowstyle='->', mutation_scale=30,
                                     linewidth=3, color='red')
        self.ax1.add_patch(self.arrow)

        self.ax1.set_xlim(-1.5, 1.5)
        self.ax1.set_ylim(-1.5, 1.5)
        self.ax1.set_aspect('equal')
        self.ax1.set_title('Compass (Top View)', fontsize=12, fontweight='bold')
        self.ax1.axis('off')

        # Add cardinal directions
        self.ax1.text(0, 1.2, 'N', ha='center', fontsize=16, fontweight='bold')
        self.ax1.text(1.2, 0, 'E', ha='center', fontsize=16, fontweight='bold')
        self.ax1.text(0, -1.2, 'S', ha='center', fontsize=16, fontweight='bold')
        self.ax1.text(-1.2, 0, 'W', ha='center', fontsize=16, fontweight='bold')

        # Add degree markers
        for angle in [45, 135, 225, 315]:
            x = 1.05 * np.sin(np.radians(angle))
            y = 1.05 * np.cos(np.radians(angle))
            self.ax1.text(x, y, f'{angle}°', ha='center', va='center', fontsize=8, color='gray')

        self.heading_text = self.ax1.text(0, -1.7, '', ha='center', fontsize=14, fontweight='bold')
        self.mag_info = self.ax1.text(0, -1.9, '', ha='center', fontsize=10)

        # 3D magnetic field plot (right)
        self.ax2 = self.fig.add_subplot(122)
        self.line_x, = self.ax2.plot([], [], 'r-', label='X (µT)', linewidth=2)
        self.line_y, = self.ax2.plot([], [], 'g-', label='Y (µT)', linewidth=2)
        self.line_z, = self.ax2.plot([], [], 'b-', label='Z (µT)', linewidth=2)
        self.ax2.set_ylim(-100, 100)
        self.ax2.set_ylabel('Magnetic Field (µT)', fontsize=12)
        self.ax2.set_xlabel('Sample', fontsize=12)
        self.ax2.legend(loc='upper right')
        self.ax2.grid(True, alpha=0.3)
        self.ax2.set_title('3D Magnetic Field Components', fontsize=12, fontweight='bold')

        # Subscribe to topic
        self.subscription = self.node.create_subscription(
            MagneticField,
            topic_name,
            self.callback,
            10
        )

    def callback(self, msg):
        """Process incoming magnetometer messages"""
        # Convert Tesla to microtesla
        x_ut = msg.magnetic_field.x * 1e6
        y_ut = msg.magnetic_field.y * 1e6
        z_ut = msg.magnetic_field.z * 1e6

        # Store data
        self.times.append(len(self.times))
        self.x_data.append(x_ut)
        self.y_data.append(y_ut)
        self.z_data.append(z_ut)

        # Calculate heading (0° = North, 90° = East)
        self.current_heading = np.degrees(np.arctan2(y_ut, x_ut))
        if self.current_heading < 0:
            self.current_heading += 360

        # Calculate magnitude
        self.current_magnitude = np.sqrt(x_ut**2 + y_ut**2 + z_ut**2)

    def update_plot(self, frame):
        """Update plot with new data"""
        # Spin ROS 2 node to process callbacks
        rclpy.spin_once(self.node, timeout_sec=0)

        if len(self.times) == 0:
            return self.arrow, self.line_x, self.line_y, self.line_z

        # Update compass arrow
        arrow_x = np.sin(np.radians(self.current_heading))
        arrow_y = np.cos(np.radians(self.current_heading))
        self.arrow.set_positions((0, 0), (arrow_x * 0.8, arrow_y * 0.8))

        # Update heading text
        self.heading_text.set_text(f'Heading: {self.current_heading:.1f}°')

        # Validate magnitude (Earth's field: 25-65 µT)
        if 25 <= self.current_magnitude <= 65:
            status = f'Magnitude: {self.current_magnitude:.1f} µT ✓\n'
            color = 'green'
        else:
            status = f'Magnitude: {self.current_magnitude:.1f} µT ⚠\n'
            color = 'orange'

        self.mag_info.set_text(status)
        self.mag_info.set_color(color)

        # Update time series
        self.line_x.set_data(self.times, self.x_data)
        self.line_y.set_data(self.times, self.y_data)
        self.line_z.set_data(self.times, self.z_data)

        # Update x-axis limits
        if len(self.times) > self.max_points:
            self.ax2.set_xlim(len(self.times) - self.max_points, len(self.times) + 10)
        else:
            self.ax2.set_xlim(0, max(self.max_points, len(self.times) + 10))

        # Auto-scale y-axis if needed
        if len(self.x_data) > 0:
            all_data = list(self.x_data) + list(self.y_data) + list(self.z_data)
            y_min, y_max = min(all_data), max(all_data)
            margin = (y_max - y_min) * 0.1 or 10
            self.ax2.set_ylim(y_min - margin, y_max + margin)

        return self.arrow, self.line_x, self.line_y, self.line_z

    def run(self):
        """Start the visualization"""
        print("\nMagnetometer Visualization")
        print("-" * 50)
        print("Test procedure:")
        print("  1. Place phone flat → compass arrow points north")
        print("  2. Rotate phone horizontally → heading changes 0-360°")
        print("  3. Verify magnitude is 25-65 µT (Earth's magnetic field)")
        print("  4. Move near metal/magnets → magnitude changes")
        print("\nPress Ctrl+C to return to Main menu\n")

        ani = FuncAnimation(self.fig, self.update_plot, interval=100, blit=False, cache_frame_data=False)
        plt.tight_layout()
        plt.show(block=True)

        # Cleanup
        cleanup_visualization()
        self.node.destroy_node()
