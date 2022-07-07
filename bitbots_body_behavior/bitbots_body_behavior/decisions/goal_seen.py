from rclpy.duration import Duration
from dynamic_stack_decider.abstract_decision_element import AbstractDecisionElement


class GoalSeen(AbstractDecisionElement):
    def __init__(self, blackboard, dsd, parameters=None):
        super(GoalSeen, self).__init__(blackboard, dsd, parameters)
        self.goal_lost_time = Duration(seconds=self.blackboard.config['goal_lost_time'])

    def perform(self, reevaluate=False):
        """
        Determines whether the goal was seen recently (as defined in config)
        :param reevaluate:
        :return:
        """
        self.publish_debug_data("goal_seen_time", self.blackboard.node.get_clock().now() - self.blackboard.world_model.goal_last_seen())
        if self.blackboard.node.get_clock().now() - self.blackboard.world_model.goal_last_seen() < self.goal_lost_time:
            return 'YES'
        return 'NO'

    def get_reevaluate(self):
        return True
