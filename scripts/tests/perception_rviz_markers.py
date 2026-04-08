#!/usr/bin/env python3
"""
Convert /cpb_*_center Point messages to rviz2 Markers for visualization.

Subscribes to detection center points and publishes colored sphere markers
on /cpb_detections_markers for rviz2 display.

Usage:
    python3 scripts/perception_rviz_markers.py

rviz2 setup:
    - Fixed Frame: zed_left_camera_frame
    - Add MarkerArray display -> topic: /cpb_detections_markers
    - Add PointCloud2 display -> topic: /zed/zed_node/point_cloud/cloud_registered
    - Optionally add PointCloud2 for /cpb_eggs, /cpb_beetle, /cpb_larva
"""

import rclpy
from rclpy.node import Node
from geometry_msgs.msg import Point
from visualization_msgs.msg import Marker, MarkerArray
from builtin_interfaces.msg import Duration


class PerceptionMarkerNode(Node):
    def __init__(self):
        super().__init__('perception_rviz_markers')

        # Class config: (topic, color_rgba, label)
        self.classes = [
            ('/cpb_beetle_center', (0.1, 0.8, 0.1, 1.0), 'beetle'),
            ('/cpb_larva_center', (0.9, 0.1, 0.1, 1.0), 'larva'),
            ('/cpb_eggs_center', (0.1, 0.1, 0.9, 1.0), 'eggs'),
        ]

        self.marker_pub = self.create_publisher(
            MarkerArray, '/cpb_detections_markers', 10)

        self.marker_id = 0
        self.frame_id = 'zed_left_camera_frame'

        # Subscribe to each class center topic
        for topic, color, label in self.classes:
            self.create_subscription(
                Point, topic,
                lambda msg, c=color, l=label: self.point_callback(msg, c, l),
                10)

        self.get_logger().info('Listening on /cpb_*_center topics, publishing markers')

    def point_callback(self, msg, color, label):
        marker = Marker()
        marker.header.frame_id = self.frame_id
        marker.header.stamp = self.get_clock().now().to_msg()
        marker.ns = label
        marker.id = self.marker_id
        self.marker_id += 1

        marker.type = Marker.SPHERE
        marker.action = Marker.ADD

        marker.pose.position = msg
        marker.pose.orientation.w = 1.0

        # 3cm sphere
        marker.scale.x = 0.03
        marker.scale.y = 0.03
        marker.scale.z = 0.03

        marker.color.r = float(color[0])
        marker.color.g = float(color[1])
        marker.color.b = float(color[2])
        marker.color.a = float(color[3])

        # Auto-expire after 1 second
        marker.lifetime = Duration(sec=1, nanosec=0)

        array = MarkerArray()
        array.markers = [marker]
        self.marker_pub.publish(array)


def main():
    rclpy.init()
    node = PerceptionMarkerNode()
    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    finally:
        node.destroy_node()
        if rclpy.ok():
            rclpy.shutdown()


if __name__ == '__main__':
    main()
