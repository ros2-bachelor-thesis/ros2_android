"""Gyroscope sensor visualizer"""

import rclpy
from rclpy.node import Node
from geometry_msgs.msg import TwistStamped
from .matplotlib_config import configure_matplotlib, cleanup_visualization
import matplotlib.pyplot as plt
configure_matplotlib()
from matplotlib.animation import FuncAnimation
from collections import deque
import numpy as np


class GyroscopeVisualizer:
    """Real-time angular velocity visualization"""

    def __init__(self, topic_name):
        self.topic_name = topic_name
        self.max_points = 100

        # Initialize ROS 2
        if not rclpy.ok():
            rclpy.init()
        self.node = Node('gyroscope_visualizer')

        # Data storage
        self.times = deque(maxlen=self.max_points)
        self.x_data = deque(maxlen=self.max_points)
        self.y_data = deque(maxlen=self.max_points)
        self.z_data = deque(maxlen=self.max_points)
        self.start_time = None

        # Create figure
        self.fig, (self.ax1, self.ax2) = plt.subplots(2, 1, figsize=(12, 8))
        self.fig.suptitle(f'Gyroscope: {topic_name}', fontsize=14, fontweight='bold')

        # Time-series plot
        self.line_x, = self.ax1.plot([], [], 'r-', label='X-axis (Roll)', linewidth=2)
        self.line_y, = self.ax1.plot([], [], 'g-', label='Y-axis (Pitch)', linewidth=2)
        self.line_z, = self.ax1.plot([], [], 'b-', label='Z-axis (Yaw)', linewidth=2)
        self.ax1.set_ylim(-5, 5)
        self.ax1.set_ylabel('Angular Velocity (rad/s)', fontsize=12)
        self.ax1.set_xlabel('Time (s)', fontsize=12)
        self.ax1.legend(loc='upper right')
        self.ax1.grid(True, alpha=0.3)
        self.ax1.axhline(y=0, color='gray', linestyle='--', alpha=0.5)

        # Status display
        self.status_text = self.ax2.text(0.5, 0.6, '', ha='center', va='center', fontsize=20)
        self.mag_text = self.ax2.text(0.5, 0.4, '', ha='center', va='center', fontsize=16)
        self.info_text = self.ax2.text(0.5, 0.2, 'Expected: ~0 rad/s when stationary',
                                       ha='center', va='center', fontsize=10, style='italic')
        self.ax2.set_xlim(0, 1)
        self.ax2.set_ylim(0, 1)
        self.ax2.axis('off')

        # Subscribe to topic
        self.subscription = self.node.create_subscription(
            TwistStamped,
            topic_name,
            self.callback,
            10
        )

    def callback(self, msg):
        """Process incoming gyroscope messages"""
        if self.start_time is None:
            self.start_time = msg.header.stamp.sec + msg.header.stamp.nanosec * 1e-9

        # Calculate relative time
        t = (msg.header.stamp.sec + msg.header.stamp.nanosec * 1e-9) - self.start_time

        # Store data
        self.times.append(t)
        self.x_data.append(msg.twist.angular.x)
        self.y_data.append(msg.twist.angular.y)
        self.z_data.append(msg.twist.angular.z)

    def update_plot(self, frame):
        """Update plot with new data"""
        # Spin ROS 2 node to process callbacks
        rclpy.spin_once(self.node, timeout_sec=0)

        if len(self.times) == 0:
            return self.line_x, self.line_y, self.line_z, self.status_text, self.mag_text

        # Update time series
        self.line_x.set_data(self.times, self.x_data)
        self.line_y.set_data(self.times, self.y_data)
        self.line_z.set_data(self.times, self.z_data)

        # Update x-axis limits
        if len(self.times) > 0:
            t_max = self.times[-1]
            self.ax1.set_xlim(max(0, t_max - 10), t_max + 1)

        # Calculate magnitude
        if len(self.x_data) > 0:
            x, y, z = self.x_data[-1], self.y_data[-1], self.z_data[-1]
            magnitude = np.sqrt(x**2 + y**2 + z**2)

            # Update display
            self.mag_text.set_text(f'Magnitude: {magnitude:.3f} rad/s')

            # Status indicator
            if magnitude < 0.1:
                status = "✓ STATIONARY"
                color = 'green'
            elif magnitude < 1.0:
                status = "↻ SLOW ROTATION"
                color = 'orange'
            else:
                status = "↻↻ FAST ROTATION"
                color = 'red'

            self.status_text.set_text(status)
            self.status_text.set_color(color)
            self.mag_text.set_color(color)

        return self.line_x, self.line_y, self.line_z, self.status_text, self.mag_text

    def run(self):
        """Start the visualization"""
        print("\nGyroscope Visualization")
        print("-" * 50)
        print("Test procedure:")
        print("  1. Keep phone stationary → all axes should read ~0 rad/s")
        print("  2. Rotate around X-axis (roll) → red line spikes")
        print("  3. Rotate around Y-axis (pitch) → green line spikes")
        print("  4. Rotate around Z-axis (yaw) → blue line spikes")
        print("\nPress Ctrl+C to return to Main menu\n")

        ani = FuncAnimation(self.fig, self.update_plot, interval=100, blit=False, cache_frame_data=False)
        plt.tight_layout()
        plt.show(block=True)

        # Cleanup
        cleanup_visualization()
        self.node.destroy_node()
