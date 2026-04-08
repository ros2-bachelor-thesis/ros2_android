"""Accelerometer sensor visualizer"""

import rclpy
from rclpy.node import Node
from geometry_msgs.msg import AccelStamped
from .matplotlib_config import configure_matplotlib, cleanup_visualization
import matplotlib.pyplot as plt
configure_matplotlib()
from matplotlib.animation import FuncAnimation
from collections import deque
import numpy as np


class AccelerometerVisualizer:
    """Real-time 3D acceleration visualization with gravity validation"""

    def __init__(self, topic_name):
        self.topic_name = topic_name
        self.max_points = 100

        # Initialize ROS 2
        if not rclpy.ok():
            rclpy.init()
        self.node = Node('accelerometer_visualizer')

        # Data storage
        self.times = deque(maxlen=self.max_points)
        self.x_data = deque(maxlen=self.max_points)
        self.y_data = deque(maxlen=self.max_points)
        self.z_data = deque(maxlen=self.max_points)
        self.start_time = None

        # Create figure
        self.fig, (self.ax1, self.ax2) = plt.subplots(2, 1, figsize=(12, 8))
        self.fig.suptitle(f'Accelerometer: {topic_name}', fontsize=14, fontweight='bold')

        # Time-series plot
        self.line_x, = self.ax1.plot([], [], 'r-', label='X-axis', linewidth=2)
        self.line_y, = self.ax1.plot([], [], 'g-', label='Y-axis', linewidth=2)
        self.line_z, = self.ax1.plot([], [], 'b-', label='Z-axis', linewidth=2)
        self.ax1.set_xlim(0, 10)  # Initial x-axis range
        self.ax1.set_ylim(-20, 20)
        self.ax1.set_ylabel('Acceleration (m/s²)', fontsize=12)
        self.ax1.set_xlabel('Time (s)', fontsize=12)
        self.ax1.legend(loc='upper right')
        self.ax1.grid(True, alpha=0.3)
        self.ax1.axhline(y=9.8, color='gray', linestyle='--', alpha=0.5, label='Gravity')
        self.ax1.axhline(y=-9.8, color='gray', linestyle='--', alpha=0.5)

        # Magnitude validation display
        self.mag_text = self.ax2.text(0.5, 0.6, '', ha='center', va='center', fontsize=20)
        self.status_text = self.ax2.text(0.5, 0.4, '', ha='center', va='center', fontsize=16)
        self.info_text = self.ax2.text(0.5, 0.2, 'Expected: ~9.8 m/s² when stationary (gravity)',
                                       ha='center', va='center', fontsize=10, style='italic')
        self.ax2.set_xlim(0, 1)
        self.ax2.set_ylim(0, 1)
        self.ax2.axis('off')

        # Subscribe to topic
        self.subscription = self.node.create_subscription(
            AccelStamped,
            topic_name,
            self.callback,
            10
        )

    def callback(self, msg):
        """Process incoming accelerometer messages"""
        if self.start_time is None:
            self.start_time = msg.header.stamp.sec + msg.header.stamp.nanosec * 1e-9

        # Calculate relative time from message timestamp
        t = (msg.header.stamp.sec + msg.header.stamp.nanosec * 1e-9) - self.start_time

        # Store data
        self.times.append(t)
        self.x_data.append(msg.accel.linear.x)
        self.y_data.append(msg.accel.linear.y)
        self.z_data.append(msg.accel.linear.z)

    def update_plot(self, frame):
        """Update plot with new data"""
        # Spin ROS 2 node to process callbacks
        rclpy.spin_once(self.node, timeout_sec=0)

        if len(self.times) == 0:
            return []


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

            # Update magnitude display
            self.mag_text.set_text(f'Magnitude: {magnitude:.2f} m/s²')

            # Status indicator
            if 8.5 < magnitude < 10.5:
                status = "✓ STATIONARY (Gravity detected)"
                color = 'green'
            else:
                status = "⚠ MOVING (Acceleration detected)"
                color = 'orange'

            self.status_text.set_text(status)
            self.mag_text.set_color(color)
            self.status_text.set_color(color)

        # Force canvas redraw
        self.fig.canvas.draw_idle()

        return [self.line_x, self.line_y, self.line_z, self.mag_text, self.status_text]

    def run(self):
        """Start the visualization"""
        print("\nAccelerometer Visualization")
        print("-" * 50)
        print("Test procedure:")
        print("  1. Place phone flat → Z-axis should read ~9.8 m/s²")
        print("  2. Rotate phone → gravity shifts between axes")
        print("  3. Shake phone → magnitude spikes above 9.8 m/s²")
        print("\nPress Ctrl+C to return to Main menu\n")

        ani = FuncAnimation(self.fig, self.update_plot, interval=100, blit=False, cache_frame_data=False)
        plt.tight_layout()
        plt.show(block=True)

        # Cleanup
        cleanup_visualization()
        self.node.destroy_node()
