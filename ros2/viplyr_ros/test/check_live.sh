#!/bin/bash
# Live source and recording check for video_publisher (run inside a ROS 2 environment
# with the package built): a paced FFmpeg test source stands in for a camera.
set -u
ros2 run viplyr_ros video_publisher --ros-args -p url:="testsrc2=size=320x240:rate=30,realtime" \
    -p input_format:=lavfi -p low_latency:=true -p record_path:=/tmp/viplyr_record.mp4 > /tmp/live.log 2>&1 &
sleep 3
echo "start: $(ros2 service call /viplyr_video_publisher/set_recording std_srvs/srv/SetBool '{data: true}' | grep -o "message=.*")"
sleep 3
echo "stop:  $(ros2 service call /viplyr_video_publisher/set_recording std_srvs/srv/SetBool '{data: false}' | grep -o "message=.*")"
pkill -f video_publisher
ffprobe -v error -select_streams v -show_entries stream=width,height,nb_frames -show_entries format=duration -of compact /tmp/viplyr_record.mp4
