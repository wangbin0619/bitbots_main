from dynamic_stack_decider.abstract_decision_element import AbstractDecisionElement


class BallDangerous(AbstractDecisionElement):
    def __init__(self, blackboard, dsd, parameters=None):
        super(BallDangerous, self).__init__(blackboard, dsd, parameters)
        self.goal_radius = self.blackboard.config['ball_dangerous_goal_radius']
        self.nofall_radius = self.blackboard.config['ball_dangerous_nofall_radius']

    def perform(self, reevaluate=False):
        """"
        Determines whether the position is in the dangerous area (in a radius close to the goal)
        """
        ball_position = self.blackboard.world_model.get_ball_position_xy()
        if self._in_dangerous_area(ball_position):
            robot_position = self.blackboard.world_model.get_current_position()
            if ball_position[1] > robot_position[1] + self.nofall_radius:
                return 'LEFT'
            elif ball_position[1] < robot_position[1] - self.nofall_radius:
                return 'RIGHT'
            return 'CENTER'
        return 'NO'

    def _in_dangerous_area(self, position):
        """"
        returns whether the position is in the dangerous area (close to the goal)
        """

        # close enough on the x axis
        if position[0] > -(self.blackboard.world_model.field_length / 2) + self.goal_radius:
            return False

        # in the y-area in front of the goal (respecting the radius
        if abs(position[1]) <= (self.blackboard.world_model.goal_width / 2 + self.goal_radius):
            return True
        return False



    def get_reevaluate(self):
        return True

