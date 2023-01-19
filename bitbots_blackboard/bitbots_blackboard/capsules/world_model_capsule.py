"""
WorldModelCapsule
^^^^^^^^^^^^^^^^^^

Provides information about the world model.
"""
import math
from typing import TYPE_CHECKING, Dict, List, Optional, Tuple

if TYPE_CHECKING:
    from bitbots_blackboard.blackboard import BodyBlackboard

import matplotlib.pyplot as plt
import numpy as np
import ros2_numpy
import tf2_ros as tf2
from bitbots_utils.utils import (get_parameter_dict,
                                 get_parameters_from_other_node)
from geometry_msgs.msg import (Point, Pose, PoseStamped,
                               PoseWithCovarianceStamped, Quaternion,
                               TransformStamped, TwistStamped,
                               TwistWithCovarianceStamped)
from nav_msgs.msg import MapMetaData, OccupancyGrid
from PIL import Image, ImageDraw
from rclpy.clock import ClockType
from rclpy.duration import Duration
from rclpy.time import Time
from scipy.interpolate import griddata
from scipy.ndimage import gaussian_filter
from sensor_msgs.msg import PointCloud2 as pc2
from soccer_vision_3d_msgs.msg import RobotArray, Robot
from std_msgs.msg import Header
from std_srvs.srv import Trigger
from tf2_geometry_msgs import PointStamped
from tf_transformations import euler_from_quaternion, quaternion_from_euler


class WorldModelCapsule:
    def __init__(self, blackboard: "BodyBlackboard"):
        self._blackboard = blackboard
        self.body_config = get_parameter_dict(self._blackboard.node, "body")
        # This pose is not supposed to be used as robot pose. Just as precision measurement for the TF position.
        self.pose = PoseWithCovarianceStamped()
        self.tf_buffer = tf2.Buffer(cache_time=Duration(seconds=30))
        self.tf_listener = tf2.TransformListener(self.tf_buffer, self._blackboard.node)

        self.odom_frame: str = self._blackboard.node.get_parameter('odom_frame').value
        self.map_frame: str = self._blackboard.node.get_parameter('map_frame').value
        self.ball_frame: str = self._blackboard.node.get_parameter('ball_frame').value
        self.base_footprint_frame: str = self._blackboard.node.get_parameter('base_footprint_frame').value

        self.ball = PointStamped()  # The ball in the base footprint frame
        self.ball_odom = PointStamped()  # The ball in the odom frame (when localization is not usable)
        self.ball_odom.header.stamp = Time(seconds=0, nanoseconds=0, clock_type=ClockType.ROS_TIME).to_msg()
        self.ball_odom.header.frame_id = self.odom_frame
        self.ball_map = PointStamped()  # The ball in the map frame (when localization is usable)
        self.ball_map.header.stamp = Time(seconds=0, nanoseconds=0, clock_type=ClockType.ROS_TIME).to_msg()
        self.ball_map.header.frame_id = self.map_frame
        self.ball_teammate = PointStamped()
        self.ball_teammate.header.stamp = Time(seconds=0, nanoseconds=0, clock_type=ClockType.ROS_TIME).to_msg()
        self.ball_teammate.header.frame_id = self.map_frame
        self.ball_lost_time = Duration(seconds=self._blackboard.node.get_parameter('body.ball_lost_time').value)
        self.ball_twist_map: Optional[TwistStamped] = None
        self.ball_filtered: Optional[PoseWithCovarianceStamped] = None
        self.ball_twist_lost_time = Duration(seconds=self._blackboard.node.get_parameter('body.ball_twist_lost_time').value)
        self.ball_twist_precision_threshold = get_parameter_dict(self._blackboard.node, 'body.ball_twist_precision_threshold')
        self.reset_ball_filter = self._blackboard.node.create_client(Trigger, 'ball_filter_reset')

        self.counter: int = 0
        self.ball_seen_time = Time(seconds=0, nanoseconds=0, clock_type=ClockType.ROS_TIME)
        self.ball_seen_time_teammate = Time(seconds=0, nanoseconds=0, clock_type=ClockType.ROS_TIME)
        self.ball_seen: bool = False
        self.ball_seen_teammate: bool = False
        parameters = get_parameters_from_other_node(
            self._blackboard.node,
            "/parameter_blackboard",
            ["field_length", "field_width", "goal_width"])
        self.field_length: float = parameters["field_length"]
        self.field_width: float = parameters["field_width"]
        self.goal_width: float = parameters["goal_width"]
        self.map_margin: float = self._blackboard.node.get_parameter('body.map_margin').value
        self.obstacle_costmap_smoothing_sigma: float = self._blackboard.node.get_parameter(
            'body.obstacle_costmap_smoothing_sigma').value
        self.obstacle_cost: float = self._blackboard.node.get_parameter('body.obstacle_cost').value

        self.localization_precision_threshold: Dict[str, float] = get_parameter_dict(
            self._blackboard.node, 'body.localization_precision_threshold')

        # Publisher for visualization in RViZ
        self.ball_publisher = self._blackboard.node.create_publisher(PointStamped, 'debug/viz_ball', 1)
        self.ball_twist_publisher = self._blackboard.node.create_publisher(TwistStamped, 'debug/ball_twist', 1)
        self.used_ball_pub = self._blackboard.node.create_publisher(PointStamped, 'debug/used_ball', 1)
        self.which_ball_pub = self._blackboard.node.create_publisher(Header, 'debug/which_ball_is_used', 1)
        self.costmap_publisher = self._blackboard.node.create_publisher(OccupancyGrid, 'debug/costmap', 1)

        self.base_costmap: Optional[np.ndarray] = None  # generated once in constructor field features
        self.costmap: Optional[np.ndarray] = None  # updated on the fly based on the base_costmap
        self.gradient_map: Optional[np.ndarray] = None  # global heading map (static) only dependent on field structure

        # Calculates the base costmap and gradient map based on it
        self.calc_base_costmap()
        self.calc_gradients()

    ############
    ### Ball ###
    ############

    def ball_seen_self(self) -> bool:
        """Returns true if we have seen the ball recently (less than ball_lost_time ago)"""
        return self._blackboard.node.get_clock().now() - self.ball_seen_time < self.ball_lost_time

    def ball_last_seen(self) -> Time:
        """
        Returns the time at which the ball was last seen if it is in the threshold or
        the more recent ball from either the teammate or itself if teamcom is available
        """
        if not self.ball_seen_self() and \
                (hasattr(self._blackboard, "team_data") and self.localization_precision_in_threshold()):
            return max(self.ball_seen_time, self._blackboard.team_data.get_teammate_ball_seen_time())
        else:
            return self.ball_seen_time

    def ball_has_been_seen(self) -> bool:
        """Returns true if we or a teammate have seen the ball recently (less than ball_lost_time ago)"""
        return self._blackboard.node.get_clock().now() - self.ball_last_seen() < self.ball_lost_time

    def get_ball_position_xy(self) -> Tuple[float, float]:
        """Return the ball saved in the map or odom frame"""
        ball = self.get_best_ball_point_stamped()
        return ball.point.x, ball.point.y

    def get_ball_stamped_relative(self) -> PointStamped:
        """ Returns the ball in the base_footprint frame i.e. relative to the robot projected on the ground"""
        return self.ball

    def get_best_ball_point_stamped(self) -> PointStamped:
        """
        Returns the best ball, either its own ball has been in the ball_lost_lost time
        or from teammate if the robot itself has lost it and teamcom is available
        """
        if self.localization_precision_in_threshold():
            if self.ball_seen_self() or not hasattr(self._blackboard, "team_data"):
                self.used_ball_pub.publish(self.ball_map)
                h = Header()
                h.stamp = self.ball_map.header.stamp
                h.frame_id = "own_ball_map"
                self.which_ball_pub.publish(h)
                return self.ball_map
            else:
                teammate_ball = self._blackboard.team_data.get_teammate_ball()
                if teammate_ball is not None and self.tf_buffer.can_transform(self.base_footprint_frame,
                                                                              teammate_ball.header.frame_id,
                                                                              teammate_ball.header.stamp,
                                                                              timeout=Duration(seconds=0.2)):
                    self.used_ball_pub.publish(teammate_ball)
                    h = Header()
                    h.stamp = teammate_ball.header.stamp
                    h.frame_id = "teammate_ball"
                    self.which_ball_pub.publish(h)
                    return teammate_ball
                else:
                    self._blackboard.node.get_logger().warning(
                        "our ball is bad but the teammates ball is worse or cant be transformed")
                    h = Header()
                    h.stamp = self.ball_map.header.stamp
                    h.frame_id = "own_ball_map"
                    self.which_ball_pub.publish(h)
                    self.used_ball_pub.publish(self.ball_map)
                    return self.ball_map
        else:
            h = Header()
            h.stamp = self.ball_odom.header.stamp
            h.frame_id = "own_ball_odom"
            self.which_ball_pub.publish(h)
            self.used_ball_pub.publish(self.ball_odom)
            return self.ball_odom

    def get_ball_position_uv(self) -> Tuple[float, float]:
        ball = self.get_best_ball_point_stamped()
        try:
            ball_bfp = self.tf_buffer.transform(ball, self.base_footprint_frame, timeout=Duration(seconds=0.2)).point
        except (tf2.ExtrapolationException) as e:
            self._blackboard.node.get_logger().warn(e)
            self._blackboard.node.get_logger().error('Severe transformation problem concerning the ball!')
            return None
        return ball_bfp.x, ball_bfp.y

    def get_ball_distance(self, filtered=False) -> float:
        if filtered:
            try:
                ball_filtered_point_stamped = PointStamped()
                ball_filtered_point_stamped.header = self.ball_filtered.header
                ball_filtered_point_stamped.point = self.ball_filtered.pose.point
                ball_bfp = self.tf_buffer.transform(self.ball_filtered_point_stamped, self.base_footprint_frame, timeout=Duration(seconds=0.2)).point
            except (tf2.ExtrapolationException) as e:
                self._blackboard.node.get_logger().warn(e)
                self._blackboard.node.get_logger().error('Severe transformation problem concerning the ball!')
                return None
            u = ball_bfp.pose.pose.position.x
            v = ball_bfp.pose.pose.position.y
        else:
            ball_pos = self.get_ball_position_uv()
            if ball_pos is None:
                return np.inf  # worst case (very far away)
            else:
                u, v = ball_pos
        return math.sqrt(u ** 2 + v ** 2)

    def get_ball_angle(self) -> float:
        ball_pos = self.get_ball_position_uv()
        if ball_pos is None:
            return -math.pi  # worst case (behind robot)
        else:
            u, v = ball_pos
        return math.atan2(v, u)

    def get_ball_speed(self) -> TwistStamped:
        raise NotImplementedError

    def ball_filtered_callback(self, msg: PoseWithCovarianceStamped):
        self.ball_filtered = msg

        # When the precision is not sufficient, the ball ages.
        x_sdev = msg.pose.covariance[0]  # position 0,0 in a 6x6-matrix
        y_sdev = msg.pose.covariance[7]  # position 1,1 in a 6x6-matrix
        if x_sdev > self.body_config['ball_position_precision_threshold']['x_sdev'] or \
                y_sdev > self.body_config['ball_position_precision_threshold']['y_sdev']:
            self.forget_ball(own=True, team=False, reset_ball_filter=False)
            return

        ball_buffer = PointStamped(header=msg.header, point=msg.pose.pose.position)
        try:
            self.ball = self.tf_buffer.transform(ball_buffer, self.base_footprint_frame, timeout=Duration(seconds=1.0))
            self.ball_odom = self.tf_buffer.transform(ball_buffer, self.odom_frame, timeout=Duration(seconds=1.0))
            self.ball_map = self.tf_buffer.transform(ball_buffer, self.map_frame, timeout=Duration(seconds=1.0))
            # Set timestamps to zero to get the newest transform when this is transformed later
            self.ball_odom.header.stamp = Time(seconds=0, nanoseconds=0, clock_type=ClockType.ROS_TIME).to_msg()
            self.ball_map.header.stamp = Time(seconds=0, nanoseconds=0, clock_type=ClockType.ROS_TIME).to_msg()
            self.ball_seen_time = self._blackboard.node.get_clock().now()
            self.ball_publisher.publish(self.ball)
            self.ball_seen = True

        except (tf2.ConnectivityException, tf2.LookupException, tf2.ExtrapolationException) as e:
            self._blackboard.node.get_logger().warn(str(e))

    def recent_ball_twist_available(self) ->  bool:
        if self.ball_twist_map is None:
            return False
        return self._blackboard.node.get_clock().now() - self.ball_twist_map.header.stamp < self.ball_twist_lost_time

    def ball_twist_callback(self, msg: TwistWithCovarianceStamped):
        x_sdev = msg.twist.covariance[0]  # position 0,0 in a 6x6-matrix
        y_sdev = msg.twist.covariance[7]  # position 1,1 in a 6x6-matrix
        if x_sdev > self.ball_twist_precision_threshold['x_sdev'] or \
                y_sdev > self.ball_twist_precision_threshold['y_sdev']:
            return
        if msg.header.frame_id != self.map_frame:
            try:
                # point (0,0,0)
                point_a = PointStamped()
                point_a.header = msg.header
                # linear velocity vector
                point_b = PointStamped()
                point_b.header = msg.header
                point_b.point.x = msg.twist.twist.linear.x
                point_b.point.y = msg.twist.twist.linear.y
                point_b.point.z = msg.twist.twist.linear.z
                # transform start and endpoint of velocity vector
                point_a = self.tf_buffer.transform(point_a, self.map_frame, timeout=Duration(seconds=1.0))
                point_b = self.tf_buffer.transform(point_b, self.map_frame, timeout=Duration(seconds=1.0))
                # build new twist using transform vector
                self.ball_twist_map = TwistStamped(header=msg.header)
                self.ball_twist_map.header.frame_id = self.map_frame
                self.ball_twist_map.twist.linear.x = point_b.point.x - point_a.point.x
                self.ball_twist_map.twist.linear.y = point_b.point.y - point_a.point.y
                self.ball_twist_map.twist.linear.z = point_b.point.z - point_a.point.z
            except (tf2.ConnectivityException, tf2.LookupException, tf2.ExtrapolationException) as e:
                self._blackboard.node.get_logger().warn(str(e))
        else:
            self.ball_twist_map = TwistStamped(header=msg.header, twist=msg.twist.twist)
        if self.ball_twist_map is not None:
            self.ball_twist_publisher.publish(self.ball_twist_map)

    def forget_ball(self, own: bool = True, team: bool = True, reset_ball_filter: bool = True) -> None:
        """
        Forget that we and the best teammate saw a ball, optionally reset the ball filter
        :param own: Forget the ball recognized by the own robot, defaults to True
        :type own: bool, optional
        :param team: Forget the ball received from the team, defaults to True
        :type team: bool, optional
        :param reset_ball_filter: Reset the ball filter, defaults to True
        :type reset_ball_filter: bool, optional
        """
        if own:  # Forget own ball
            self.ball_seen_time = Time(seconds=0, nanoseconds=0, clock_type=ClockType.ROS_TIME)
            self.ball = PointStamped()

        if team:  # Forget team ball
            self.ball_seen_time_teammate = Time(seconds=0, nanoseconds=0, clock_type=ClockType.ROS_TIME)
            self.ball_teammate = PointStamped()

        if reset_ball_filter:  # Reset the ball filter
            result: Trigger.Response = self.reset_ball_filter.call(Trigger.Request())
            if result.success:
                self._blackboard.node.get_logger().info(f"Received message from ball filter: '{result.message}'")
            else:
                self._blackboard.node.get_logger().warn(f"Ball filter reset failed with: '{result.message}'")

    ########
    # Goal #
    ########

    def get_map_based_opp_goal_center_uv(self):
        x, y = self.get_map_based_opp_goal_center_xy()
        return self.get_uv_from_xy(x, y)

    def get_map_based_opp_goal_center_xy(self):
        return self.field_length / 2, 0.0

    def get_map_based_own_goal_center_uv(self):
        x, y = self.get_map_based_own_goal_center_xy()
        return self.get_uv_from_xy(x, y)

    def get_map_based_own_goal_center_xy(self):
        return -self.field_length / 2, 0.0

    def get_map_based_opp_goal_angle_from_ball(self):
        ball_x, ball_y = self.get_ball_position_xy()
        goal_x, goal_y = self.get_map_based_opp_goal_center_xy()
        return math.atan2(goal_y - ball_y, goal_x - ball_x)

    def get_map_based_opp_goal_distance(self):
        x, y = self.get_map_based_opp_goal_center_xy()
        return self.get_distance_to_xy(x, y)

    def get_map_based_opp_goal_angle(self):
        x, y = self.get_map_based_opp_goal_center_uv()
        return math.atan2(y, x)

    def get_map_based_opp_goal_left_post_uv(self):
        x, y = self.get_map_based_opp_goal_center_xy()
        return self.get_uv_from_xy(x, y - self.goal_width / 2)

    def get_map_based_opp_goal_right_post_uv(self):
        x, y = self.get_map_based_opp_goal_center_xy()
        return self.get_uv_from_xy(x, y + self.goal_width / 2)

    ########
    # Pose #
    ########

    def pose_callback(self, pos: PoseWithCovarianceStamped):
        self.pose = pos

    def get_current_position(self) -> Tuple[float, float, float]:
        """
        Returns the current position as determined by the localization
        :returns x,y,theta
        """
        transform = self.get_current_position_transform()
        if transform is None:
            return None
        orientation = transform.transform.rotation
        theta = euler_from_quaternion([orientation.x, orientation.y, orientation.z, orientation.w])[2]
        return transform.transform.translation.x, transform.transform.translation.y, theta

    def get_current_position_pose_stamped(self) -> PoseStamped:
        """
        Returns the current position as determined by the localization as a PoseStamped
        """
        transform = self.get_current_position_transform()
        if transform is None:
            return None
        ps = PoseStamped()
        ps.header = transform.header
        ps.pose.position.x = transform.transform.translation.x
        ps.pose.position.y = transform.transform.translation.y
        ps.pose.position.z = transform.transform.translation.z
        ps.pose.orientation = transform.transform.rotation
        return ps

    def get_current_position_transform(self) -> TransformStamped:
        """
        Returns the current position as determined by the localization as a TransformStamped
        """
        try:
            # get the most recent transform
            transform = self.tf_buffer.lookup_transform(self.map_frame, self.base_footprint_frame,
                                                        Time(seconds=0, nanoseconds=0, clock_type=ClockType.ROS_TIME))
        except (tf2.LookupException, tf2.ConnectivityException, tf2.ExtrapolationException) as e:
            self._blackboard.node.get_logger().warn(str(e))
            return None
        return transform

    def get_localization_precision(self) -> Tuple[float, float, float]:
        """
        Returns the current localization precision based on the covariance matrix.
        """
        x_sdev = self.pose.pose.covariance[0]  # position 0,0 in a 6x6-matrix
        y_sdev = self.pose.pose.covariance[7]  # position 1,1 in a 6x6-matrix
        theta_sdev = self.pose.pose.covariance[35]  # position 5,5 in a 6x6-matrix
        return (x_sdev, y_sdev, theta_sdev)

    def localization_precision_in_threshold(self) -> bool:
        """
        Returns whether the last localization precision values were in the threshold defined in the settings.
        """
        # Check whether we can transform into and from the map frame seconds.
        if not self.localization_pose_current():
            return False
        # get the standard deviation values of the covariance matrix
        precision = self.get_localization_precision()
        # return whether those values are in the threshold
        return precision[0] < self.localization_precision_threshold['x_sdev'] and \
               precision[1] < self.localization_precision_threshold['y_sdev'] and \
               precision[2] < self.localization_precision_threshold['theta_sdev']

    def localization_pose_current(self) -> bool:
        """
        Returns whether we can transform into and from the map frame.
        """
        # if we can do this, we should be able to transform the ball
        # (unless the localization dies in the next 0.2 seconds)
        try:
            t = self._blackboard.node.get_clock().now() - Duration(seconds=0.3)
        except TypeError as e:
            self._blackboard.node.get_logger().error(e)
            t = Time(seconds=0, nanoseconds=0, clock_type=ClockType.ROS_TIME)
        return self.tf_buffer.can_transform(self.base_footprint_frame, self.map_frame, t)

    ##########
    # Common #
    ##########

    def get_uv_from_xy(self, x, y) -> Tuple[float, float]:
        """ Returns the relativ positions of the robot to this absolute position"""
        current_position = self.get_current_position()
        x2 = x - current_position[0]
        y2 = y - current_position[1]
        theta = -1 * current_position[2]
        u = math.cos(theta) * x2 + math.sin(theta) * y2
        v = math.cos(theta) * y2 - math.sin(theta) * x2
        return u, v

    def get_xy_from_uv(self, u, v):
        """ Returns the absolute position from the given relative position to the robot"""
        pos_x, pos_y, theta = self.get_current_position()
        angle = math.atan2(v, u) + theta
        hypotenuse = math.sqrt(u ** 2 + v ** 2)
        return pos_x + math.sin(angle) * hypotenuse, pos_y + math.cos(angle) * hypotenuse

    def get_distance_to_xy(self, x, y):
        """ Returns distance from robot to given position """
        u, v = self.get_uv_from_xy(x, y)
        dist = math.sqrt(u ** 2 + v ** 2)
        return dist

    ############
    # Obstacle #
    ############

    def robot_obstacle_callback(self, msg: RobotArray):
        """
        Callback with new obstacles
        """
        # Init a new obstacle costmap
        obstacle_map = np.zeros_like(self.costmap)
        # Iterate over all obstacles
        robot: Robot
        for robot in msg.robots:
            # Convert position to array index
            idx_x, idx_y = self.field_2_costmap_coord(robot.bb.center.position.x, robot.bb.center.position.x)
            # TODO inflate
            # Draw obstacle with smoothing independent weight on obstacle costmap
            obstacle_map[idx_x, idx_y] = \
                self.obstacle_cost * self.obstacle_costmap_smoothing_sigma
        # Smooth obstacle map
        obstacle_map = gaussian_filter(obstacle_map, self.obstacle_costmap_smoothing_sigma)
        # Get pass offsets
        self.pass_map = self.get_pass_regions()
        # Merge costmaps
        self.costmap = self.base_costmap.copy() + obstacle_map - self.pass_map
        # Publish debug costmap
        self.costmap_debug_draw()

    def costmap_debug_draw(self):
        """
        Publishes the costmap for rviz
        """
        # Normalize costmap to match the rviz color scheme in a good way
        normalized_costmap = (255 - ((self.costmap - np.min(self.costmap)) / (
                    np.max(self.costmap) - np.min(self.costmap))) * 255 / 2.1).astype(np.int8).T
        # Build the OccupancyGrid message
        msg: OccupancyGrid = ros2_numpy.msgify(
            OccupancyGrid,
            normalized_costmap,
            info=MapMetaData(
                resolution=0.1,
                origin=Pose(
                    position=Point(
                        x=-self.field_length / 2 - self.map_margin,
                        y=-self.field_width / 2 - self.map_margin,
                    )
                )))
        # Change the frame to allow namespaces
        msg.header.frame_id = self.map_frame
        # Publish
        self.costmap_publisher.publish(msg)

    def get_pass_regions(self) -> np.ndarray:
        """
        Draws a costmap for the pass regions
        """
        pass_dist = 1.0
        pass_weight = 20.0
        pass_smooth = 4.0
        # Init a new costmap
        costmap = np.zeros_like(self.costmap)
        # Iterate over possible team mate poses
        for pose in self._blackboard.team_data.get_active_teammate_poses(count_goalies=False):
            # Get positions
            goal_position = np.array([self.field_length / 2, 0, 0])  # position of the opponent goal
            teammate_position = ros2_numpy.numpify(pose.position)
            # Get vector
            vector_teammate_to_goal = goal_position - ros2_numpy.numpify(pose.position)
            # Position between robot and goal but 1m away from the robot
            pass_pos = vector_teammate_to_goal / np.linalg.norm(vector_teammate_to_goal) * pass_dist + teammate_position
            # Convert position to array index
            idx_x, idx_y = self.field_2_costmap_coord(pass_pos[0], pass_pos[1])
            # Draw pass position with smoothing independent weight on costmap
            costmap[idx_x, idx_y] = pass_weight * pass_smooth
        # Smooth obstacle map
        return gaussian_filter(costmap, pass_smooth)

    def field_2_costmap_coord(self, x: float, y: float) -> Tuple[float, float]:
        """
        Converts a field position to the coresponding indices for the costmap.

        :param x: X Position relative to the center point. (Positive is towards the enemy goal)
        :param y: Y Position relative to the center point. (Positive is towards the left when we face the enemy goal)
        :return: The x index of the coresponding costmap slot, The y index of the coresponding costmap slot
        """
        idx_x = int(min(((self.field_length + self.map_margin * 2) * 10) - 1,
                        max(0, (x + self.field_length / 2 + self.map_margin) * 10)))
        idx_y = int(min(((self.field_width + self.map_margin * 2) * 10) - 1,
                        max(0, (y + self.field_width / 2 + self.map_margin) * 10)))
        return idx_x, idx_y

    def calc_gradients(self):
        """
        Recalculates the gradient map based on the current costmap.
        """
        gradient = np.gradient(self.base_costmap)
        norms = np.linalg.norm(gradient, axis=0)

        # normalize gradient length
        gradient = [np.where(norms == 0, 0, i / norms) for i in gradient]
        self.gradient_map = gradient

    def cost_at_relative_xy(self, x: float, y: float) -> float:
        """
        Returns cost at relative position to the base footprint.
        """
        if self.costmap is None:
            return 0.0

        point = PointStamped()
        point.header.stamp = Time(seconds=0, nanoseconds=0, clock_type=ClockType.ROS_TIME).to_msg()
        point.header.frame_id = self.base_footprint_frame
        point.point.x = x
        point.point.y = y

        try:
            # Transform point of interest to the map
            point = self.tf_buffer.transform(point, self.map_frame, timeout=Duration(seconds=0.3))
        except (tf2.ConnectivityException, tf2.LookupException, tf2.ExtrapolationException) as e:
            self._blackboard.node.get_logger().warn(e)
            return 0.0

        return self.get_cost_at_field_position(point.point.x, point.point.y)

    def calc_base_costmap(self):
        """
        Builds the base costmap based on the bahavior parameters.
        This costmap includes a gradient towards the enemy goal and high costs outside the playable area
        """
        # Get parameters
        goalpost_safety_distance: float = self._blackboard.node.get_parameter(
            "body.goalpost_safety_distance").value  # offset in y direction from the goalpost
        keep_out_border: float = self._blackboard.node.get_parameter(
            "body.keep_out_border").value  # dangerous border area
        in_field_value_our_side: float = self._blackboard.node.get_parameter(
            "body.in_field_value_our_side").value  # start value on our side
        corner_value: float = self._blackboard.node.get_parameter(
            "body.corner_value").value  # cost in a corner
        goalpost_value: float = self._blackboard.node.get_parameter(
            "body.goalpost_value").value  # cost at a goalpost
        goal_value: float = self._blackboard.node.get_parameter(
            "body.goal_value").value  # cost in the goal

        # Create Grid
        grid_x, grid_y = np.mgrid[
            0:self.field_length + self.map_margin * 2:(self.field_length + self.map_margin * 2) * 10j,
            0:self.field_width + self.map_margin * 2:(self.field_width + self.map_margin * 2) * 10j]

        fix_points: List[Tuple[Tuple[float, float], float]] = []

        # Add base points
        fix_points.extend([
            # Corner points of the map (including margin)
            ((-self.map_margin, -self.map_margin),
             corner_value + in_field_value_our_side),
            ((self.field_length + self.map_margin, -self.map_margin),
             corner_value + in_field_value_our_side),
            ((-self.map_margin, self.field_width + self.map_margin),
             corner_value + in_field_value_our_side),
            ((self.field_length + self.map_margin, self.field_width + self.map_margin),
             corner_value + in_field_value_our_side),
            # Corner points of the field
            ((0, 0),
             corner_value + in_field_value_our_side),
            ((self.field_length, 0),
             corner_value),
            ((0, self.field_width),
             corner_value + in_field_value_our_side),
            ((self.field_length, self.field_width),
             corner_value),
            # Points in the field that pull the gradient down, so we don't play always in the middle
            ((keep_out_border, keep_out_border),
             in_field_value_our_side),
            ((keep_out_border, self.field_width - keep_out_border),
             in_field_value_our_side),
        ])

        # Add goal area (including the dangerous parts on the side of the goal)
        fix_points.extend([
            ((self.field_length, self.field_width / 2 - self.goal_width / 2),
             goalpost_value),
            ((self.field_length, self.field_width / 2 + self.goal_width / 2),
             goalpost_value),
            ((self.field_length, self.field_width / 2 - self.goal_width / 2 + goalpost_safety_distance),
             goal_value),
            ((self.field_length, self.field_width / 2 + self.goal_width / 2 - goalpost_safety_distance),
             goal_value),
            ((self.field_length + self.map_margin,
              self.field_width / 2 - self.goal_width / 2 - goalpost_safety_distance),
             -0.2),
            ((self.field_length + self.map_margin,
              self.field_width / 2 + self.goal_width / 2 + goalpost_safety_distance),
             -0.2),
        ])

        # Apply map margin to fixpoints
        fix_points = [((p[0][0] + self.map_margin, p[0][1] + self.map_margin), p[1]) for p in fix_points]

        # Interpolate the keypoints from above to form the costmap
        interpolated = griddata([p[0] for p in fix_points], [p[1] for p in fix_points], (grid_x, grid_y),
                                method='linear')

        # Smooth the costmap to get more continus gradients
        self.base_costmap = gaussian_filter(
            interpolated,
            self._blackboard.node.get_parameter(
                "body.base_costmap_smoothing_sigma").value)
        self.costmap = self.base_costmap.copy()

    def get_gradient_at_field_position(self, x: float, y: float) -> Tuple[float, float]:
        """
        Gets the gradient tuple at a given field position
        :param x: Field coordiante in the x direction
        :param y: Field coordiante in the y direction
        """
        idx_x, idx_y = self.field_2_costmap_coord(x, y)
        return -self.gradient_map[0][idx_x, idx_y], -self.gradient_map[1][idx_x, idx_y]

    def get_cost_at_field_position(self, x: float, y: float) -> float:
        """
        Gets the costmap value at a given field position
        :param x: Field coordinate in the x direction
        :param y: Field coordinate in the y direction
        """
        idx_x, idx_y = self.field_2_costmap_coord(x, y)
        return self.costmap[idx_x, idx_y]

    def get_gradient_direction_at_field_position(self, x: float, y: float):
        """
        Returns the gradient direction at the given position
        :param x: Field coordiante in the x direction
        :param y: Field coordiante in the y direction
        """
        # for debugging only
        #if False and self.costmap.sum() > 0:
        #    # Create Grid
        #    grid_x, grid_y = np.mgrid[0:self.field_length:self.field_length * 10j,
        #                     0:self.field_width:self.field_width * 10j]
        #    plt.imshow(self.costmap.T, origin='lower')
        #    plt.show()
        #    plt.quiver(grid_x, grid_y, -self.gradient_map[0], -self.gradient_map[1])
        #    plt.show()

        grad = self.get_gradient_at_field_position(x, y)
        return math.atan2(grad[1], grad[0])

    def get_cost_of_kick_relative(self, x: float, y: float, direction: float, kick_length: float, angular_range: float):
        if self.costmap is None:
            return 0.0

        pose = PoseStamped()
        pose.header.stamp = Time(seconds=0, nanoseconds=0, clock_type=ClockType.ROS_TIME).to_msg()
        pose.header.frame_id = self.base_footprint_frame
        pose.pose.position.x = float(x)
        pose.pose.position.y = float(y)

        x, y, z, w = quaternion_from_euler(0, 0, direction)
        pose.pose.orientation = Quaternion(x=x, y=y, z=z, w=w)
        try:
            # Transform point of interest to the map
            pose = self.tf_buffer.transform(pose, self.map_frame, timeout=Duration(seconds=0.3))

        except (tf2.ConnectivityException, tf2.LookupException, tf2.ExtrapolationException) as e:
            self._blackboard.node.get_logger().warn(e)
            return 0.0
        d = euler_from_quaternion(
            [pose.pose.orientation.x, pose.pose.orientation.y, pose.pose.orientation.z, pose.pose.orientation.w])[2]
        return self.get_cost_of_kick(pose.pose.position.x, pose.pose.position.y, d, kick_length, angular_range)

    def get_cost_of_kick(self, x: float, y: float, direction: float, kick_length: float, angular_range: float) -> float:

        # create a mask in the size of the costmap consisting of 8-bit values initialized as 0
        mask = Image.new('L', (self.costmap.shape[1], self.costmap.shape[0]))

        # draw kick area on mask with ones
        maskd = ImageDraw.Draw(mask)
        # axes are switched in pillow

        b, a = self.field_2_costmap_coord(x, y)
        k = kick_length * 10
        m = a + k * math.sin(direction + 0.5 * angular_range)
        n = b + k * math.sin(0.5 * math.pi - (direction + 0.5 * angular_range))
        o = a + k * math.sin(direction - 0.5 * angular_range)
        p = b + k * math.sin(0.5 * math.pi - (direction - 0.5 * angular_range))
        maskd.polygon(((a, b), (m, n), (o, p)), fill=1)

        mask_array = np.array(mask)

        masked_costmap = self.costmap * mask_array

        # plt.imshow(self.costmap, origin='lower')
        # plt.show()
        # plt.imshow(masked_costmap, origin='lower')
        # plt.show()

        # The main influence should be the maximum cost in the area which is covered by the kick. This could be the field boundary, robots, ...
        # But we also want prio directions with lower min cost. This could be the goal area or the pass accept area of an teammate
        # This should contribute way less than the max and should have an impact if the max values are similar in all directions.
        return masked_costmap.max() * 0.75 + masked_costmap.min() * 0.25

    def get_current_cost_of_kick(self, direction: float, kick_length: float, angular_range: float):
        return self.get_cost_of_kick_relative(0, 0, direction, kick_length, angular_range)

    def get_best_kick_direction(self, min_angle: float, max_angle: float, num_kick_angles: int, kick_length: float, angular_range: float) -> float:
        # list of possible kick directions, sorted by absolute value to
        # prefer forward kicks to side kicks if their costs are equal
        kick_directions = sorted(np.linspace(min_angle,
                                             max_angle,
                                             num=num_kick_angles), key=abs)

        # get the kick direction with the least cost
        kick_direction = kick_directions[np.argmin([self.get_current_cost_of_kick(direction=direction,
                                                                                  kick_length=kick_length,
                                                                                  angular_range=angular_range)
                                                    for direction in kick_directions])]
        return kick_direction
