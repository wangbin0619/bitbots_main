import math
import numpy as np
from bitbots_blackboard.blackboard import BodyBlackboard
from geometry_msgs.msg import Twist

from dynamic_stack_decider.abstract_action_element import AbstractActionElement


class DribbleForward(AbstractActionElement):
    blackboard: BodyBlackboard
    def __init__(self, blackboard, dsd, parameters=None):
        super().__init__(blackboard, dsd, parameters)
        self.max_speed_x = self.blackboard.config['dribble_max_speed_x']
        self.min_speed_x = -0.1
        self.max_speed_y = self.blackboard.config['dribble_max_speed_y']
        self.walk_backward_angle = math.radians(45)
        self.p = self.blackboard.config['dribble_p']
        self.max_accel_x = self.blackboard.config['dribble_accel_x']

        self.current_speed_x = self.blackboard.pathfinding.current_cmd_vel.linear.x
        self.current_speed_y = self.blackboard.pathfinding.current_cmd_vel.linear.y

    def perform(self, reevaluate=False):
        """
        Dribbles the ball forward. It uses a simple P-controller to publish the corresponding velocities directly.

        :param reevaluate:
        :return:
        """
        # Get the ball relative to the base fottprint
        ball_u, ball_v = self.blackboard.world_model.get_ball_position_uv()
        # Get the relative angle from us to the ball
        ball_angle = self.blackboard.world_model.get_ball_angle()

        # todo compute yaw speed based on how we are aligned to the goal

        adaptive_acceleration_x =  1 - (abs(ball_angle) / self.walk_backward_angle)
        x_speed = self.max_speed_x * adaptive_acceleration_x

        self.current_speed_x = \
            self.max_accel_x * np.clip(x_speed, self.min_speed_x, self.max_speed_x) + \
            self.current_speed_x * (1 - self.max_accel_x)

        # give more speed in y direction based on ball position
        y_speed = ball_v * self.p
        self.current_speed_y = np.clip(y_speed, -self.max_speed_y, self.max_speed_y)

        cmd_vel = Twist()
        cmd_vel.linear.x = self.current_speed_x
        cmd_vel.linear.y = self.current_speed_y
        cmd_vel.angular.z = 0.0
        self.blackboard.pathfinding.direct_cmd_vel_pub.publish(cmd_vel)
