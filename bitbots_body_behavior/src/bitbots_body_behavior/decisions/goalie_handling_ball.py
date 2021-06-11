import rospy

from dynamic_stack_decider.abstract_decision_element import AbstractDecisionElement


class GoalieHandlingToBall(AbstractDecisionElement):
    def __init__(self, blackboard, dsd, parameters=None):
        super().__init__(blackboard, dsd, parameters)

    def perform(self, reevaluate=False):
        """
        It is determined if the goalie is currently going towards the ball
        """
        if self.blackboard.team_data.is_goalie_handling_ball:
            return "YES"
        else:
            return "FALSE"

    def get_reevaluate(self):
        return True
