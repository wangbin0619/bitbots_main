#ifndef BITBOTS_DYNAMIC_KICK_KICK_ENGINE_H
#define BITBOTS_DYNAMIC_KICK_KICK_ENGINE_H

#include <optional>
#include <bitbots_splines/SmoothSpline.hpp>
#include <bitbots_splines/SplineContainer.hpp>
#include <bitbots_msgs/KickGoal.h>
#include <bitbots_msgs/KickFeedback.h>
#include <tf/LinearMath/Transform.h>
#include "Stabilizer.h"

typedef bitbots_splines::SplineContainer<bitbots_splines::SmoothSpline> Trajectories;

class KickParams {
public:
    double trunk_movement = 0.06;  // meters to move trunk to the left (i.e. support foot to the right)
    double foot_rise = 0.08;        // how much the foot is raised before kicking
    double kick_distance = 0.12;    // kick distance to the front
    double trunk_kick_pitch = -0.2;
    double trunk_kick_roll = 0.2;
    double foot_rise_trunk_movement;
    double trunk_height = 0.4;
    double foot_distance = 0.12;
    double kick_windup_distance = 0.2;

    double move_trunk_time = 1;
    double raise_foot_time = 1;
    double move_to_ball_time = 1;
    double kick_time = 1;
    double move_back_time = 1;
    double lower_foot_time = 1;

    double stabilizing_point;
};

/**
 * The KickEngine takes care of choosing an optimal foot to reach a given goal,
 * planning that foots required movement (rotation and positioning)
 * and updating short-term MotorGoals to move (the foot) along that planned path.
 *
 * It is vital to call the engines tick() method repeatedly because that is where these short-term MotorGoals are
 * returned.
 *
 * The KickEngine utilizes a Stabilizer to balance the robot during foot movments.
 */
class KickEngine {
public:
    KickEngine();

    /**
     * Set new goal which the engine tries to kick at. This will remove the old goal completely and plan new splines.
     * @param target_pose Pose in base_link frame which should be reached.
     *      Ideally this is also where the ball lies and a kick occurs.
     * @param speed Speed in each dimension with which to kick the ball // TODO document scale of speed (m/s, km/h, 0-1,...)
     * @param trunk_pose Current pose of left foot in base_link frame
     * @param r_foot_pose Current pos of right foot in base_link frame
     */
    void set_goal(const geometry_msgs::Pose &target_pose, tf2::Vector3 speed,
                  const geometry_msgs::Pose &trunk_pose, const geometry_msgs::Pose &r_foot_pose,
                  bool is_left_kick);

    /**
     * Reset this KickEngine completely, removing the goal, all splines and thereby stopping all output
     */
    void reset();

    /**
     * Do one iteration of spline-progress-updating. This means that whenever tick() is called,
     *      new position goals are retrieved from previously calculated splines, stabilized and transformed into
     *      JointGoals
     * @param dt Passed delta-time between last call to tick() and now. Measured in seconds
     * @return New motor goals only if a goal is currently set, position extractions from splines was possible and
     *      bio_ik was able to compute valid motor positions
     */
    std::optional<JointGoals> tick(double dt);

    /**
     * Is the currently performed kick with the left foot or not
     */
    bool is_left_kick();
    int get_percent_done() const;
    void set_params(KickParams params);
    Stabilizer m_stabilizer;
private:
    double m_time;
    geometry_msgs::Pose m_goal_pose;
    tf2::Vector3 m_speed;
    bool m_is_left_kick;
    std::optional<Trajectories> m_support_point_trajectories, m_flying_trajectories;
    KickParams m_params;

    /**
     * Construct m_trajectories and add all required splines with their respective keys
     */
    void init_trajectories();

    /**
     *  // TODO Add docstring to calc_splines once we resolve issue #2
     */
    void calc_splines(const geometry_msgs::Pose& target_pose, const geometry_msgs::Pose& r_foot_pose,
                      const geometry_msgs::Pose& trunk_pose);
    geometry_msgs::PoseStamped get_current_pose(Trajectories spline_container);
};

#endif  // BITBOTS_DYNAMIC_KICK_KICK_ENGINE_H
