import rclpy
from rclpy.node import Node
from rclpy.qos import QoSProfile, ReliabilityPolicy, HistoryPolicy
from sensor_msgs.msg import Image, CompressedImage
from std_msgs.msg import String, Float32MultiArray
from geometry_msgs.msg import Twist
import cv2
import numpy as np
import os

class ExampleInferenceNode(Node):
    def __init__(self):
        super().__init__("example_inference_node")
        self.get_logger().info("Example Inference Node started")

        # Setup QoS
        qos = QoSProfile(
            reliability=ReliabilityPolicy.BEST_EFFORT,
            history=HistoryPolicy.KEEP_LAST,
            depth=10
        )
        
        # Initialize publishers
        self.action_pub = self.create_publisher(
            Float32MultiArray, 
            '/example/actions', 
            10
        )
        
        # Initialize subscribers
        self.image_sub = self.create_subscription(
            Image,
            '/camera/image_raw',
            self.image_callback,
            qos
        )
    
    def image_callback(self, msg: Image):
        # TODO: Implement image processing
        pass

    def get_action(self, Float32MultiArray) -> Float32MultiArray:
        # TODO: Implement action processing
        # self.action_pub.publish(action)
        pass



def main(args=None):
    rclpy.init(args=args)
    
    try:
        # Create and spin the node
        node = ExampleInferenceNode()
        
        print("Example Inference Node starting...")
        
        # Spin the node
        rclpy.spin(node)
        
    except KeyboardInterrupt:
        print("Node interrupted by user")
    except Exception as e:
        print(f"Node failed with error: {e}")
    finally:
        # Clean shutdown
        try:
            node.cleanup()
            node.destroy_node()
        except:
            pass
        rclpy.shutdown()


if __name__ == '__main__':
    main()
