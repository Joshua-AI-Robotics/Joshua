"""Launch camera and encoder publishers."""
import launch
import launch_ros.actions


def generate_launch_description():
    """Launch camera and encoder publishers."""
    return launch.LaunchDescription([
        launch_ros.actions.Node(
            executable='ros2/camera_publisher',
            output='screen',
            name='camera_publisher'),
        launch_ros.actions.Node(
            executable='ros2/encoder_publisher',
            output='screen',
            name='encoder_publisher'),
    ])