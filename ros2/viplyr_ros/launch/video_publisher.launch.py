"""ros2 launch viplyr_ros video_publisher.launch.py url:=/path/video.mp4 [hw:=auto] [encoding:=bgr8]"""
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node


def generate_launch_description():
    arguments = {"url": "", "hw": "auto", "encoding": "bgr8", "rate": "1.0", "loop": "true", "frame_id": "camera", "reliable": "false",
                 "width": "0", "height": "0", "low_latency": "false", "input_format": "", "record_path": ""}
    return LaunchDescription(
        [DeclareLaunchArgument(name, default_value=value) for name, value in arguments.items()]
        + [
            Node(
                package="viplyr_ros",
                executable="video_publisher",
                name="viplyr_video_publisher",
                parameters=[{name: LaunchConfiguration(name) for name in arguments}],
                output="screen",
            )
        ]
    )
