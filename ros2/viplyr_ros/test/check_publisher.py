"""Checks a running video_publisher: frame rate, image size and encoding, pause/play and seek.

Usage: python3 check_publisher.py <width> <height> <fps>
"""
import sys
import time

import rclpy
from rclpy.node import Node
from rclpy.qos import qos_profile_sensor_data
from sensor_msgs.msg import CameraInfo, Image
from std_msgs.msg import Float64
from std_srvs.srv import SetBool

failures = 0


def check(condition, text):
    global failures
    print(("  ok: " if condition else "  FAILED: ") + text)
    if not condition:
        failures += 1


class Checker(Node):
    def __init__(self):
        super().__init__("viplyr_publisher_check")
        self.images = []
        self.infos = 0
        self.create_subscription(Image, "image_raw", self.images.append, qos_profile_sensor_data)
        self.create_subscription(CameraInfo, "camera_info", self.count_info, qos_profile_sensor_data)
        self.seek = self.create_publisher(Float64, "viplyr_video_publisher/seek", 10)
        self.set_playing = self.create_client(SetBool, "viplyr_video_publisher/set_playing")

    def count_info(self, _):
        self.infos += 1

    def spin_for(self, seconds):
        end = time.monotonic() + seconds
        while time.monotonic() < end:
            rclpy.spin_once(self, timeout_sec=0.05)

    def call_set_playing(self, playing):
        future = self.set_playing.call_async(SetBool.Request(data=playing))
        rclpy.spin_until_future_complete(self, future, timeout_sec=3.0)
        return future.result() is not None and future.result().success


def main():
    width, height, fps = int(sys.argv[1]), int(sys.argv[2]), float(sys.argv[3])
    rclpy.init()
    node = Checker()
    # Discovery and start-up: wait for the first image, then let the stream settle.
    deadline = time.monotonic() + 15.0
    while not node.images and time.monotonic() < deadline:
        rclpy.spin_once(node, timeout_sec=0.05)
    check(bool(node.images), "images arrive")
    node.spin_for(1.0)

    node.images.clear()
    node.spin_for(2.0)
    rate = len(node.images) / 2.0
    check(abs(rate - fps) < fps * 0.15, f"about {fps} images/s, got {rate:.1f}")
    check(node.infos > 0, "camera_info published")
    if node.images:
        image = node.images[-1]
        check((image.width, image.height) == (width, height), f"size {image.width}x{image.height}")
        check(image.encoding == "bgr8" and image.step == width * 3, f"encoding {image.encoding}, step {image.step}")
        check(len(image.data) == image.step * image.height, "data size")
        check(image.header.frame_id == "camera", "frame_id")

    check(node.set_playing.wait_for_service(timeout_sec=3.0), "set_playing service available")
    check(node.call_set_playing(False), "pause")
    node.spin_for(0.5)
    node.images.clear()
    node.spin_for(1.0)
    check(len(node.images) == 0, f"no images while paused, got {len(node.images)}")

    node.seek.publish(Float64(data=1.0))  # a seek while paused shows the new frame
    node.spin_for(1.0)
    check(len(node.images) == 1, f"one image after a paused seek, got {len(node.images)}")

    check(node.call_set_playing(True), "play")
    node.spin_for(0.3)
    node.images.clear()
    node.spin_for(2.0)
    rate = len(node.images) / 2.0
    check(abs(rate - fps) < fps * 0.15, f"about {fps} images/s again after play, got {rate:.1f}")

    node.destroy_node()
    rclpy.shutdown()
    print("ALL PASSED" if failures == 0 else f"{failures} FAILURE(S)")
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
