import math
import numpy as np
import rospy
from bitbots_msgs.msg import KickGoal
from geometry_msgs.msg import Quaternion
from tf.transformations import quaternion_from_euler

from dynamic_stack_decider.abstract_action_element import AbstractActionElement


class AbstractKickAction(AbstractActionElement):
    def pop(self):
        self.blackboard.world_model.forget_ball()
        super(AbstractKickAction, self).pop()


class KickBallStatic(AbstractKickAction):
    def __init__(self, blackboard, dsd, parameters=None):
        super(KickBallStatic, self).__init__(blackboard, dsd, parameters)
        if 'foot' not in parameters.keys():
            # usually, we kick with the right foot
            self.kick = 'kick_right'  # TODO get actual name of parameter from some config
        elif 'right' == parameters['foot']:
            self.kick = 'kick_right'  # TODO get actual name of parameter from some config
        elif 'left' == parameters['foot']:
            self.kick = 'kick_left'  # TODO get actual name of parameter from some config
        else:
            rospy.logerr(
                'The parameter \'{}\' could not be used to decide which foot should kick'.format(parameters['foot']))

    def perform(self, reevaluate=False):
        if not self.blackboard.animation.is_animation_busy():
            self.blackboard.animation.play_animation(self.kick)


class KickBallDynamic(AbstractKickAction):
    """
    Kick the ball using bitbots_dynamic_kick
    """

    def __init__(self, blackboard, dsd, parameters=None):
        super(KickBallDynamic, self).__init__(blackboard, dsd, parameters)
        if parameters.get('type', None) == 'penalty':
            self.penalty_kick = True
        else:
            self.penalty_kick = False

        self._goal_sent = False

    def perform(self, reevaluate=False):
        self.do_not_reevaluate()

        if not self.blackboard.kick.is_currently_kicking:
            if not self._goal_sent:
                goal = KickGoal()
                goal.header.stamp = rospy.Time.now()

                # TODO evaluate whether the dynamic kick is good enough to actually use the ball position
                # currently we use a tested left or right kick
                ball_u, ball_v = self.blackboard.world_model.get_ball_position_uv()
                goal.header.frame_id = self.blackboard.world_model.base_footprint_frame  # the ball position is stated in this frame
                goal.ball_position.x = ball_u
                goal.ball_position.y = ball_v
                goal.ball_position.z = 0

                #rospy.logerr((self.blackboard.world_model.obstacle_value_at_relative_xy(1, 0, 2), self.blackboard.world_model.obstacle_value_at_relative_xy(0.5, -0.5, 2), self.blackboard.world_model.obstacle_value_at_relative_xy(0.5, 0.5, 2)))

                check_positions = [(1, 0), (0, -1), (0, 1)]
                kick_directions = [0, -1.2, 1.2]

                kick_direction = kick_directions[np.argmin(
                    list(map(lambda t: self.blackboard.world_model.obstacle_value_at_relative_xy(*t, 3), check_positions)))]

                goal.kick_direction = Quaternion(*quaternion_from_euler(0, 0, kick_direction))

                if self.penalty_kick:
                    goal.kick_speed = 3
                else:
                    goal.kick_speed = 1

                self.blackboard.kick.kick(goal)
                self._goal_sent = True

            else:
                self.pop()


class KickBallVeryHard(AbstractKickAction):
    def __init__(self, blackboard, dsd, parameters=None):
        super(KickBallVeryHard, self).__init__(blackboard, dsd, parameters)
        if 'foot' not in parameters.keys():
            # usually, we kick with the right foot
            self.hard_kick = 'kick_right'  # TODO get actual name of parameter from some config
        elif 'right' == parameters['foot']:
            self.hard_kick = 'kick_right'  # TODO get actual name of parameter from some config
        elif 'left' == parameters['foot']:
            self.hard_kick = 'kick_left'  # TODO get actual name of parameter from some config
        else:
            rospy.logerr(
                'The parameter \'{}\' could not be used to decide which foot should kick'.format(parameters['foot']))

    def perform(self, reevaluate=False):
        if not self.blackboard.animation.is_animation_busy():
            self.blackboard.animation.play_animation(self.hard_kick)
