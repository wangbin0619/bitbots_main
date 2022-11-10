#!/usr/bin/env python3
import argparse
import os
import threading

import rclpy
from rcl_interfaces.srv import GetParameters
from rclpy.node import Node
import time
from wolfgang_webots_sim.webots_robot_controller import RobotController
from controller import Robot


class RobotNode:
    def __init__(self, pid_param_name, robot_name, void_controller, disable_camera, recognize, robot_type):
        self.node = Node('robot_node')
        self.void_controller = void_controller

        blackboard_client = self.node.create_client(GetParameters, '/parameter_blackboard/get_parameters')
        while not blackboard_client.wait_for_service(timeout_sec=3.0):
            self.node.get_logger().info('blackboard not available, waiting again...')
        req = GetParameters.Request(names=[pid_param_name])
        while True:
            future = blackboard_client.call_async(req)
            rclpy.spin_until_future_complete(self.node, future)
            if future.result() is not None:
                break
            else:
                self.node.get_logger().info("Waiting for parameter " + pid_param_name + " to be set..")
                time.sleep(2.0)
        webots_pid = future.result().values[0]

        #os.environ["WEBOTS_PID"] = str(webots_pid)
        os.environ["WEBOTS_CONTROLLER_URL"] = f"ipc://6009/{robot_name}"

        if void_controller:
            self.node.get_logger().info("Starting void interface for " + robot_name)
            self.robot = Robot()
        else:
            self.node.get_logger().info("Starting ros interface for " + robot_name)
            self.robot = RobotController(ros_node=self.node, robot=robot_type, ros_active=True, recognize=recognize, camera_active=(not disable_camera))

    def run(self):
        while rclpy.ok():
            if self.void_controller:
                self.robot.step(int(self.robot.getBasicTimeStep()))
            else:
                self.robot.step()


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument('--robot_name', help="which robot should be started", default="amy")
    parser.add_argument('--robot-type', help="which robot should be started", default="wolfgang")
    parser.add_argument('--sim_id', help="identifier of the simulation", default="")
    parser.add_argument('--void-controller', action='store_true',
                        help="if true, a controller that only steps and does nothing else")
    parser.add_argument('--disable-camera', action='store_true',
                        help="Turn on or off the camera (to speed up if only motion is required)")
    parser.add_argument('--recognize', action='store_true',
                        help="if true, recognition is active (for training data collection)")
    args, unknown = parser.parse_known_args()
    pid_param_name = "webots_pid" + args.sim_id

    rclpy.init()
    robot = RobotNode(pid_param_name, args.robot_name, args.void_controller, args.disable_camera, args.recognize, args.robot_type)
    thread = threading.Thread(target=rclpy.spin, args=(robot.node,), daemon=True)
    thread.start()
    robot.run()

    robot.node.destroy_node()
    rclpy.shutdown()
