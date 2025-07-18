"""Launch camera and encoder publishers."""
import launch
from launch.actions import DeclareLaunchArgument, LogInfo
from launch.substitutions import TextSubstitution
import launch_ros.actions
from launch_ros.actions import Node
from launch.substitutions import LaunchConfiguration

def generate_launch_description():
    # Declare a launch argument for the node prefix
    node_prefix_arg = DeclareLaunchArgument(
        'node_prefix',
        default_value=TextSubstitution(text='my_robot'),
        description='Prefix for node names to avoid conflicts'
    )

    # Declare a launch argument for the message frequency of the talker
    message_frequency_arg = DeclareLaunchArgument(
        'message_frequency',
        default_value='1.0',
        description='Frequency of messages published by the talker in Hz'
    )


    return launch.LaunchDescription([
        # Debug output
        LogInfo(msg='Starting ROS2 launcher...'),
        LogInfo(msg='Camera publisher executable: bazel-bin/ros2/camera_publisher'),
        LogInfo(msg='Encoder publisher executable: bazel-bin/ros2/encoder_publisher'),
        
        node_prefix_arg,
        message_frequency_arg,

        Node(
            executable='/home/hmoon/Projects/ProjectJoshua/bazel-bin/ros2/camera_publisher',
            output='screen',
            name=[LaunchConfiguration('node_prefix'), '_camera_publisher'],
            parameters=[{
                'message_frequency': LaunchConfiguration('message_frequency'),
            }],
        ),
        
        Node(
            executable='/home/hmoon/Projects/ProjectJoshua/bazel-bin/ros2/encoder_publisher',
            output='screen',
            name=[LaunchConfiguration('node_prefix'), '_encoder_publisher'],
            parameters=[{
                'message_frequency': LaunchConfiguration('message_frequency'),
            }],
        ),
    ])