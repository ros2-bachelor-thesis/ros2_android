"""Illuminance (light sensor) visualizer"""

import rclpy
from rclpy.node import Node
from sensor_msgs.msg import Illuminance
from .matplotlib_config import configure_matplotlib, cleanup_visualization
import matplotlib.pyplot as plt
configure_matplotlib()
from matplotlib.animation import FuncAnimation
from matplotlib.patches import Circle
import numpy as np


class IlluminanceVisualizer:
    """Animated circle brightness visualization for light sensor"""

    def __init__(self, topic_name):
        self.topic_name = topic_name
        self.current_lux = 0

        # Initialize ROS 2
        if not rclpy.ok():
            rclpy.init()
        self.node = Node('illuminance_visualizer')

        # Create figure
        self.fig, self.ax = plt.subplots(figsize=(10, 8))
        self.fig.suptitle(f'Light Sensor: {topic_name}', fontsize=14, fontweight='bold', y=0.97)

        # Create circle - centered but moved up slightly to make room for legend
        self.circle = Circle((0.5, 0.55), 0.3, color='white', ec='black', linewidth=2)
        self.ax.add_patch(self.circle)
        self.ax.set_xlim(0, 1)
        self.ax.set_ylim(0, 1)
        self.ax.set_aspect('equal')
        self.ax.axis('off')

        # Text displays - positioned relative to circle
        self.lux_text = self.ax.text(0.5, 0.55, '', ha='center', va='center', fontsize=16)
        self.status_text = self.ax.text(0.5, 0.9, 'Waiting for data...',
                                       ha='center', fontsize=14, fontweight='bold')

        # Legend - compact format at bottom with more spacing
        legend_text = (
            'Light Levels:\n'
            'Very Dark < 10 lx  |  Dark 10-50 lx  |  Dim 50-200 lx\n'
            'Normal Indoor 200-1k lx  |  Bright Indoor 1k-10k lx\n'
            'Outdoor Shade 10k-50k lx  |  Direct Sunlight > 50k lx'
        )
        self.ax.text(0.5, 0.05, legend_text, ha='center', fontsize=7,
                    style='italic', color='gray', verticalalignment='bottom')

        # Subscribe to topic
        self.subscription = self.node.create_subscription(
            Illuminance,
            topic_name,
            self.callback,
            10
        )

    def callback(self, msg):
        """Process incoming illuminance messages"""
        self.current_lux = msg.illuminance

    def lux_to_brightness(self, lux):
        """Map lux to 0-1 brightness using logarithmic scale"""
        min_lux, max_lux = 0.1, 100000
        brightness = (np.log10(max(lux, min_lux)) - np.log10(min_lux)) / \
                     (np.log10(max_lux) - np.log10(min_lux))
        return np.clip(brightness, 0, 1)

    def get_light_level(self, lux):
        """Classify light level"""
        if lux < 10:
            return "Very Dark", "darkviolet"
        elif lux < 50:
            return "Dark", "navy"
        elif lux < 200:
            return "Dim", "blue"
        elif lux < 1000:
            return "Normal Indoor", "green"
        elif lux < 10000:
            return "Bright Indoor", "orange"
        elif lux < 50000:
            return "Outdoor Shade", "darkorange"
        else:
            return "Direct Sunlight", "red"

    def update_plot(self, frame):
        """Update plot with new data"""
        brightness = self.lux_to_brightness(self.current_lux)

        # Update circle color (grayscale)
        self.circle.set_facecolor((brightness, brightness, brightness))

        # Get light level classification
        level, level_color = self.get_light_level(self.current_lux)

        # Update text
        self.lux_text.set_text(f'{self.current_lux:.1f} lx')
        self.status_text.set_text(level)

        # Text color depends on brightness (white on dark, black on light)
        text_color = 'white' if brightness < 0.5 else 'black'
        self.lux_text.set_color(text_color)

        # Status text uses level color
        self.status_text.set_color(level_color)

        # Spin ROS 2 node
        rclpy.spin_once(self.node, timeout_sec=0)

        return self.circle, self.lux_text, self.status_text

    def run(self):
        """Start the visualization"""
        print("\nIlluminance (Light Sensor) Visualization")
        print("-" * 50)
        print("Test procedure:")
        print("  1. Cover sensor with hand → circle darkens (1-10 lx)")
        print("  2. Normal indoor lighting → medium brightness (100-500 lx)")
        print("  3. Point at bright light → circle brightens (1000+ lx)")
        print("  4. Go outside → maximum brightness (10,000-100,000 lx)")
        print("\nThe circle brightness changes logarithmically with lux value")
        print("\nPress Ctrl+C to return to Main menu\n")
        ani = FuncAnimation(self.fig, self.update_plot, interval=50, blit=False, cache_frame_data=False)
        plt.subplots_adjust(left=0.05, right=0.95, top=0.95, bottom=0.12)
        plt.show(block=True)

        # Cleanup
        cleanup_visualization()
        self.node.destroy_node()
