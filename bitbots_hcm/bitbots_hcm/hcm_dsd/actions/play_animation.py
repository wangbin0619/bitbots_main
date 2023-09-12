from action_msgs.msg import GoalStatus
from bitbots_hcm.hcm_dsd.hcm_blackboard import HcmBlackboard

import rclpy
from bitbots_msgs.action import Dynup
from dynamic_stack_decider.abstract_action_element import AbstractActionElement
from humanoid_league_msgs.action import PlayAnimation
from humanoid_league_msgs.msg import RobotControlState


class AbstractPlayAnimation(AbstractActionElement):
    """
    Abstract class to create actions for playing animations
    """

    def __init__(self, blackboard, dsd, parameters=None):
        super().__init__(blackboard, dsd, parameters)
        self.blackboard: HcmBlackboard
        self.first_perform = True

    def perform(self, reevaluate=False):
        # we never want to leave the action when we play an animation
        self.do_not_reevaluate()

        if self.first_perform:
            # get the animation that should be played
            # defined by implementations of this abstract class
            anim = self.chose_animation()

            # try to start animation
            sucess = self.start_animation(anim)

            # if we fail, we need to abort this action
            if not sucess:
                self.blackboard.node.get_logger().error("Could not start animation. Will abort play animation action!")
                return self.pop()

            self.first_perform = False
            return

        if self.animation_finished():
            # we are finished playing this animation
            return self.pop()

    def chose_animation(self):
        # this is what has to be implemented returning the animation to play
        raise NotImplementedError

    def start_animation(self, anim):
        """
        This will NOT wait by itself. You have to check
        animation_finished()
        by yourself.
        :param anim: animation to play
        :return:
        """

        self.blackboard.node.get_logger().info("Playing animation " + anim)
        if anim is None or anim == "":
            self.blackboard.node.get_logger().warn("Tried to play an animation with an empty name!")
            return False
        first_try = self.blackboard.animation_action_client.wait_for_server(
            timeout_sec=self.blackboard.node.get_parameter('hcm.anim_server_wait_time').get_parameter_value().double_value)
        if not first_try:
            server_running = False
            while not server_running and not self.blackboard.shut_down_request and rclpy.ok():
                self.blackboard.node.get_logger().warn(
                                      "Animation Action Server not running! Motion can not work without animation action server. "
                                      "Will now wait until server is accessible!",
                                      throttle_duration_sec=10.0)
                server_running = self.blackboard.animation_action_client.wait_for_server(timeout_sec=1)
            if server_running:
                self.blackboard.node.get_logger().warn("Animation server now running, hcm will go on.")
            else:
                self.blackboard.node.get_logger().warn("Animation server did not start.")
                return False
        goal = PlayAnimation.Goal()
        goal.animation = anim
        goal.hcm = True  # the animation is from the hcm
        self.blackboard.animation_action_current_goal = self.blackboard.animation_action_client.send_goal_async(goal)
        return True

    def animation_finished(self):
        return self.blackboard.animation_action_current_goal.cancelled() or self.blackboard.animation_action_current_goal.done()


class PlayAnimationFallingLeft(AbstractPlayAnimation):
    def chose_animation(self):
        self.blackboard.node.get_logger().info("PLAYING FALLING LEFT ANIMATION")
        return self.blackboard.falling_animation_left


class PlayAnimationFallingRight(AbstractPlayAnimation):
    def chose_animation(self):
        self.blackboard.node.get_logger().info("PLAYING FALLING RIGHT ANIMATION")
        return self.blackboard.falling_animation_right


class PlayAnimationFallingFront(AbstractPlayAnimation):
    def chose_animation(self):
        self.blackboard.node.get_logger().info("PLAYING FALLING FRONT ANIMATION")
        return self.blackboard.falling_animation_front


class PlayAnimationFallingBack(AbstractPlayAnimation):
    def chose_animation(self):
        self.blackboard.node.get_logger().info("PLAYING FALLING BACK ANIMATION")
        return self.blackboard.falling_animation_back


class PlayAnimationDynup(AbstractActionElement):
    def __init__(self, blackboard, dsd, parameters=None):
        super().__init__(blackboard, dsd, parameters)
        self.blackboard: HcmBlackboard
        self.direction = parameters.get('direction')
        self.first_perform = True

        # A parameter 'initial' is passed when dynup is called during the startup phase,
        # in this case we do not want to set the state to GETTING_UP.
        initial = parameters.get('initial', False)
        if not initial:
            self.blackboard.current_state = RobotControlState.GETTING_UP

    def perform(self, reevaluate=False):
        # deactivate falling since it will be wrongly detected
        self.do_not_reevaluate()
        if self.first_perform:
            # get the animation that should be played
            # defined by implementations of this abstract class

            # try to start animation
            success = self.start_animation()
            # if we fail, we need to abort this action
            if not success:
                self.blackboard.node.get_logger().error("Could not start animation. Will abort play animation action!")
                return self.pop()

            self.first_perform = False
            return

        if self.animation_finished():
            # we are finished playing this animation
            return self.pop()

    def start_animation(self):
        """
        This will NOT wait by itself. You have to check animation_finished() by yourself.
        :return:
        """

        first_try = self.blackboard.dynup_action_client.wait_for_server(
            timeout_sec=self.blackboard.node.get_parameter('hcm.anim_server_wait_time').get_parameter_value().double_value)
        if not first_try:
            server_running = False
            while not server_running and not self.blackboard.shut_down_request and rclpy.ok():
                self.blackboard.node.get_logger().warn(
                                      "Dynup Action Server not running! Dynup cannot work without dynup server!"
                                      "Will now wait until server is accessible!",
                                      throttle_duration_sec=10.0)
                server_running = self.blackboard.dynup_action_client.wait_for_server(timeout_sec=1)
            if server_running:
                self.blackboard.node.get_logger().warn("Dynup server now running, hcm will go on.")
            else:
                self.blackboard.node.get_logger().warn("Dynup server did not start.")
                return False
        goal = Dynup.Goal()
        goal.direction = self.direction
        self.blackboard.dynup_action_current_goal = self.blackboard.dynup_action_client.send_goal_async(goal)
        return True

    def animation_finished(self):
        return (self.blackboard.dynup_action_current_goal.done() and self.blackboard.dynup_action_current_goal.result().status == GoalStatus.STATUS_SUCCEEDED) \
                or self.blackboard.dynup_action_current_goal.cancelled()
