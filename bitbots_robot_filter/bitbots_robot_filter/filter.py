# Copyright (c) 2022 Hamburg Bit-Bots
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.


import numpy as np
import tf2_ros as tf2
import tf2_geometry_msgs

from humanoid_league_msgs.msg import TeamData
from std_msgs.msg import Header
import rclpy
from rclpy.time import Time
from rclpy.duration import Duration
from rclpy.executors import MultiThreadedExecutor
from rclpy.node import Node
from geometry_msgs.msg import Pose
import soccer_vision_3d_msgs.msg as sv3dm
import soccer_vision_attribute_msgs.msg as svam
import tf2_ros as tf2
from ros2_numpy import numpify


class RobotFilter(Node):
    def __init__(self):
        super().__init__('bitbots_robot_filter')

        self.tf_buffer = tf2.Buffer(cache_time=Duration(seconds=10.0))
        self.tf_listener = tf2.TransformListener(self.tf_buffer, self)

        self.declare_parameter('map_frame', 'map')
        self.map_frame = self.get_parameter('map_frame').value

        self.create_subscription(sv3dm.RobotArray, 'robots_relative', self._robot_vision_callback, 5)
        self.create_subscription(TeamData, 'team_data', self._team_data_callback, 5)

        self.robot_obstacle_publisher = self.create_publisher(sv3dm.RobotArray, "robots_relative_filtered", 1)

        self.robots = []

        self.team = dict()

        self.robots_storage_time = 10e9
        self.robot_merge_distance = 0.5
        self.robot_dummy_size = 0.4

        self.create_timer(1/20, self.publish_obstacles)

    def publish_obstacles(self):
        # Set current timespamp and frame
        dummy_header = Header()
        dummy_header.stamp = self.get_clock().now().to_msg()
        dummy_header.frame_id = self.map_frame

        # Cleanup robot obstacles
        self.robots = list(filter(
            lambda robot: abs((self.get_clock().now() - Time.from_msg(robot[1])).nanoseconds) < self.robots_storage_time,
            self.robots))


        # Add Team Mates
        def build_robot_detection_from_team_data(msg: TeamData) -> sv3dm.Robot:
            robot = sv3dm.Robot()
            robot.bb.center = msg.robot_position.pose
            robot.bb.size.x = self.robot_dummy_size
            robot.bb.size.y = self.robot_dummy_size
            robot.bb.size.z = 1.0
            robot.attributes.team = svam.Robot.TEAM_OWN
            robot.attributes.player_number = msg.robot_id
            return (robot, msg.header.stamp)

        self.robots.extend(list(map(build_robot_detection_from_team_data, self.team.values())))

        msg = sv3dm.RobotArray()
        msg.header = dummy_header
        msg.robots = [robot_msg for robot_msg, _ in self.robots]

        self.robot_obstacle_publisher.publish(msg)

    def _robot_vision_callback(self, msg: sv3dm.RobotArray):
        try:
            transform = self.tf_buffer.lookup_transform(self.map_frame,
                                                        msg.header.frame_id,
                                                        msg.header.stamp,
                                                        Duration(seconds=1.0))
        except (tf2.ConnectivityException, tf2.LookupException, tf2.ExtrapolationException) as e:
            self.get_logger().warn(str(e))
            return

        robot: sv3dm.Robot
        for robot in msg.robots:
            # Transfrom robot to map frame
            robot.bb.center: Pose = tf2_geometry_msgs.do_transform_pose(robot.bb.center, transform)
            # Update robots that are close together
            cleaned_robots = []
            old_robot: sv3dm.Robot
            for old_robot, stamp in self.robots:
                # Check if there is another robot in memory close to it
                if np.linalg.norm(numpify(robot.bb.center.position) - numpify(old_robot.bb.center.position)) > self.robot_merge_distance:
                    cleaned_robots.append((old_robot, stamp))
            # Update our robot list with a new list that does not contain the duplicates
            self.robots = cleaned_robots
            # Append our new robots
            self.robots.append((robot, msg.header.stamp))

    def _team_data_callback(self, msg):
        self.team[msg.robot_id] = msg


def main(args=None):
    rclpy.init(args=args)
    node = RobotFilter()
    ex = MultiThreadedExecutor(num_threads=4)
    ex.add_node(node)
    ex.spin()
    node.destroy_node()
    rclpy.shutdown()
