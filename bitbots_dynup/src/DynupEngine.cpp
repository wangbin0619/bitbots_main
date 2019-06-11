#include "bitbots_dynup/DynupEngine.h"

DynupEngine::DynupEngine() : m_listener(m_tf_buffer) {}

void DynupEngine::reset() {
    m_time = 0;
    m_support_point_trajectories.reset();
    m_flying_trajectories.reset();
}

bool DynupEngine::set_goal(bool front) {
        /* Plan new splines according to new goal */
        init_trajectories();
        calc_splines(m_is_left_kick ? l_foot_pose : r_foot_pose);
        return true;
}

std::optional<JointGoals> DynupEngine::tick(double dt) {
    /* Only do an actual tick when splines are present */
    if (m_support_point_trajectories && m_flying_trajectories) {
        /* Get should-be pose from planned splines (every axis) at current time */
        geometry_msgs::Point support_point;
        support_point.x = m_support_point_trajectories.value().get("pos_x").pos(m_time);
        support_point.y = m_support_point_trajectories.value().get("pos_y").pos(m_time);
        geometry_msgs::PoseStamped flying_foot_pose = get_current_pose(m_flying_trajectories.value());

        m_time += dt;

        /* Stabilize and return result */
        return m_stabilizer.stabilize(m_is_left_kick, support_point, flying_foot_pose);
    } else {
        return std::nullopt;
    }
}

geometry_msgs::PoseStamped DynupEngine::get_current_pose(Trajectories spline_container) {
    geometry_msgs::PoseStamped foot_pose;
    foot_pose.header.frame_id = "l_sole";
    foot_pose.header.stamp = ros::Time::now();
    foot_pose.pose.position.x = spline_container.get("pos_x").pos(m_time);
    foot_pose.pose.position.y = spline_container.get("pos_y").pos(m_time);
    foot_pose.pose.position.z = spline_container.get("pos_z").pos(m_time);
    tf2::Quaternion q;
    /* Apparently, the axis order is different than expected */
    q.setEuler(spline_container.get("pitch").pos(m_time),
               spline_container.get("roll").pos(m_time),
               spline_container.get("yaw").pos(m_time));
    foot_pose.pose.orientation.x = q.x();
    foot_pose.pose.orientation.y = q.y();
    foot_pose.pose.orientation.z = q.z();
    foot_pose.pose.orientation.w = q.w();
    return foot_pose;
}

void DynupEngine::calc_front_splines(const geometry_msgs::Pose &foot_pose){
    /*
    calculates splines for front up
    */

    /*
     * start spline point with current poses
     */
    // hand
    m_hand_trajectories->get("pos_x").addPoint(time_start, hand_pose.position.x);
    m_hand_trajectories->get("pos_y").addPoint(time_start, hand_pose.position.y);
    m_hand_trajectories->get("pos_z").addPoint(time_start, hand_pose.position.z);

    /* Construct a start_rotation as quaternion from Pose msg */
    tf::Quaternion hand_start_rotation(flying_hand_pose.orientation.x, flying_hand_pose.orientation.y,
                                  flying_hand_pose.orientation.z, flying_hand_pose.orientation.w);
    double hand_start_r, hand_start_p, hand_start_y;
    tf::Matrix3x3(hand_start_rotation).getRPY(hand_start_r, hand_start_p, hand_start_y);
    m_hand_trajectories->get("roll").addPoint(time_start, start_r);
    m_hand_trajectories->get("pitch").addPoint(time_start, start_p);
    m_hand_trajectories->get("yaw").addPoint(time_start, start_y);

    // foot
    time_start = 0;

    m_foot_trajectories->get("pos_x").addPoint(time_start, foot_pose.position.x);
    m_foot_trajectories->get("pos_y").addPoint(time_start, foot_pose.position.y);
    m_foot_trajectories->get("pos_z").addPoint(time_start, foot_pose.position.z);

    /* Construct a start_rotation as quaternion from Pose msg */
    tf::Quaternion foot_start_rotation(flying_foot_pose.orientation.x, flying_foot_pose.orientation.y,
                                  flying_foot_pose.orientation.z, flying_foot_pose.orientation.w);
    double foot_start_r, foot_start_p, foot_start_y;
    tf::Matrix3x3(foot_start_rotation).getRPY(foot_start_r, foot_start_p, foot_start_y);
    m_foot_trajectories->get("roll").addPoint(time_start, foot_start_r);
    m_foot_trajectories->get("pitch").addPoint(time_start, foot_start_p);
    m_foot_trajectories->get("yaw").addPoint(time_start, foot_start_y);


    //TODO spline in between to enable the hands to go to the front

    /*
     * pull legs to body
    */
    time_foot_close = 0; // TODO
    m_foot_trajectories->get("pos_x").addPoint(time_foot_close, 0);
    m_foot_trajectories->get("pos_y").addPoint(time_foot_close, 0);
    m_foot_trajectories->get("pos_z").addPoint(time_foot_close, m_params.leg_min_length);
    m_foot_trajectories->get("roll").addPoint(time_foot_close, 0);
    m_foot_trajectories->get("pitch").addPoint(time_foot_close, 0);
    m_foot_trajectories->get("yaw").addPoint(time_foot_close, 0);


    /*
     * hands to the front
     */
    time_hands_front = 0; //TODO parameter
    m_hand_trajectories->get("pos_x").addPoint(time_hands_front, 0);
    m_hand_trajectories->get("pos_y").addPoint(time_hands_front, 0);
    m_hand_trajectories->get("pos_z").addPoint(time_hands_front, m_params.arm_max_length);
    m_hand_trajectories->get("roll").addPoint(time_hands_front, 0);
    m_hand_trajectories->get("pitch").addPoint(time_hands_front, math.pi);
    m_hand_trajectories->get("yaw").addPoint(time_hands_front, 0);

    /*
     * Foot under body
     */
    time_foot_ground = 0; //TODO
    m_foot_trajectories->get("pos_x").addPoint(time_foot_ground, 0);
    m_foot_trajectories->get("pos_y").addPoint(time_foot_ground, 0);
    m_foot_trajectories->get("pos_z").addPoint(time_foot_ground, m_params.leg_min_length);
    m_foot_trajectories->get("roll").addPoint(time_foot_ground, 0);
    m_foot_trajectories->get("pitch").addPoint(time_foot_ground, math.pi);
    m_foot_trajectories->get("yaw").addPoint(time_foot_ground, 0);


    /*
     * Torso 45°
     */
    time_torso_45 = 0; //TODO
    m_hand_trajectories->get("pos_x").addPoint(time_torso_45, m_params.arm_max_length);
    m_hand_trajectories->get("pos_y").addPoint(time_torso_45, 0);
    m_hand_trajectories->get("pos_z").addPoint(time_torso_45, 0);
    m_hand_trajectories->get("roll").addPoint(time_torso_45, 0);
    m_hand_trajectories->get("pitch").addPoint(time_torso_45, 0);
    m_hand_trajectories->get("yaw").addPoint(time_torso_45, 0);

    m_foot_trajectories->get("pos_x").addPoint(time_torso_45, 0);
    m_foot_trajectories->get("pos_y").addPoint(time_torso_45, 0);
    m_foot_trajectories->get("pos_z").addPoint(time_torso_45, m_params.leg_min_length);
    m_foot_trajectories->get("roll").addPoint(time_torso_45, 0);
    m_foot_trajectories->get("pitch").addPoint(time_torso_45, math.pi);
    m_foot_trajectories->get("yaw").addPoint(time_torso_45, 0);



}

void DynupEngine::calc_splines(const geometry_msgs::Pose &flying_foot_pose) {
    /*
     * Add current position, target position and current position to splines so that they describe a smooth
     * curve to the ball and back
     */
    /* Splines:
     * - if front:
     *   - move arms to frint and pull legs
     *   - get torso into 45°, pull foot under legs
     *   - get into crouch position
     * - if back:
     *
     * - after both:
     *    - slowly stand up with stabilization
     *    - move arms in finish position
     */
    /* The fix* variables describe the discrete points in time where the positions are given by the parameters.
     * Between them, the spline interpolation happens. */
    double fix0 = 0;
    double fix1 = fix0 + m_params.move_trunk_time;
    double fix2 = fix1 + m_params.raise_foot_time;
    double fix3 = fix2 + m_params.move_to_ball_time;
    double fix4 = fix3 + m_params.kick_time;
    double fix5 = fix4 + m_params.move_back_time;
    double fix6 = fix5 + m_params.lower_foot_time;
    double fix7 = fix6 + m_params.move_trunk_back_time;

    int kick_foot_sign;
    if (m_is_left_kick) {
        kick_foot_sign = 1;
    } else {
        kick_foot_sign = -1;
    }

    tf2::Vector3 kick_windup_point = calc_kick_windup_point();

    /* Flying foot position */
    m_flying_trajectories->get("pos_x").addPoint(fix0, flying_foot_pose.position.x);
    m_flying_trajectories->get("pos_x").addPoint(fix1, 0);
    m_flying_trajectories->get("pos_x").addPoint(fix2, 0);
    m_flying_trajectories->get("pos_x").addPoint(fix3, kick_windup_point.x(), 0, 0);
    m_flying_trajectories->get("pos_x").addPoint(fix4, m_ball_position.x, m_kick_movement.x);
    m_flying_trajectories->get("pos_x").addPoint(fix5, 0);
    m_flying_trajectories->get("pos_x").addPoint(fix6, 0);
    m_flying_trajectories->get("pos_x").addPoint(fix7, 0);

    m_flying_trajectories->get("pos_y").addPoint(fix0, flying_foot_pose.position.y);
    m_flying_trajectories->get("pos_y").addPoint(fix1, kick_foot_sign * m_params.foot_distance);
    m_flying_trajectories->get("pos_y").addPoint(fix2, kick_foot_sign * m_params.foot_distance);
    m_flying_trajectories->get("pos_y").addPoint(fix3, kick_windup_point.y(), 0, 0);
    m_flying_trajectories->get("pos_y").addPoint(fix4, m_ball_position.y, m_kick_movement.y);
    m_flying_trajectories->get("pos_y").addPoint(fix5, kick_foot_sign * m_params.foot_distance);
    m_flying_trajectories->get("pos_y").addPoint(fix6, kick_foot_sign * m_params.foot_distance);
    m_flying_trajectories->get("pos_y").addPoint(fix7, kick_foot_sign * m_params.foot_distance);

    m_flying_trajectories->get("pos_z").addPoint(fix0, flying_foot_pose.position.z);
    m_flying_trajectories->get("pos_z").addPoint(fix1, 0);
    m_flying_trajectories->get("pos_z").addPoint(fix2, m_params.foot_rise);
    m_flying_trajectories->get("pos_z").addPoint(fix3, m_params.foot_rise);
    m_flying_trajectories->get("pos_z").addPoint(fix4, m_params.foot_rise);
    m_flying_trajectories->get("pos_z").addPoint(fix5, m_params.foot_rise);
    m_flying_trajectories->get("pos_z").addPoint(fix6, 0);
    m_flying_trajectories->get("pos_z").addPoint(fix7, 0);

    /* Flying foot orientation */
    /* Construct a start_rotation as quaternion from Pose msg */
    tf::Quaternion start_rotation(flying_foot_pose.orientation.x, flying_foot_pose.orientation.y,
                                  flying_foot_pose.orientation.z, flying_foot_pose.orientation.w);
    double start_r, start_p, start_y;
    tf::Matrix3x3(start_rotation).getRPY(start_r, start_p, start_y);

    /* Also construct one for the target */
    tf::Quaternion target_rotation(flying_foot_pose.orientation.x, flying_foot_pose.orientation.y,
                                   flying_foot_pose.orientation.z, flying_foot_pose.orientation.w);
    double target_r, target_p, target_y;
    tf::Matrix3x3(target_rotation).getRPY(target_r, target_p, target_y);

    target_y = calc_kick_foot_yaw();

    /* Add these quaternions in the same fashion as before to our splines (current, target, current) */
    m_flying_trajectories->get("roll").addPoint(fix0, start_r);
    m_flying_trajectories->get("roll").addPoint(fix3, start_r);
    m_flying_trajectories->get("roll").addPoint(fix7, start_r);
    m_flying_trajectories->get("pitch").addPoint(fix0, start_p);
    m_flying_trajectories->get("pitch").addPoint(fix3, start_p);
    m_flying_trajectories->get("pitch").addPoint(fix7, start_p);
    m_flying_trajectories->get("yaw").addPoint(fix0, start_y);
    m_flying_trajectories->get("yaw").addPoint(fix3, target_y);
    m_flying_trajectories->get("yaw").addPoint(fix7, start_y);

    /* Stabilizing point */
    m_support_point_trajectories->get("pos_x").addPoint(fix0, 0);
    m_support_point_trajectories->get("pos_x").addPoint(fix1, m_params.stabilizing_point_x);
    m_support_point_trajectories->get("pos_x").addPoint(fix2, m_params.stabilizing_point_x);
    m_support_point_trajectories->get("pos_x").addPoint(fix3, m_params.stabilizing_point_x);
    m_support_point_trajectories->get("pos_x").addPoint(fix4, m_params.stabilizing_point_x);
    m_support_point_trajectories->get("pos_x").addPoint(fix5, m_params.stabilizing_point_x);
    m_support_point_trajectories->get("pos_x").addPoint(fix6, m_params.stabilizing_point_x);
    m_support_point_trajectories->get("pos_x").addPoint(fix7, 0);

    m_support_point_trajectories->get("pos_y").addPoint(fix0, kick_foot_sign * (m_params.foot_distance / 2.0));
    m_support_point_trajectories->get("pos_y").addPoint(fix1, kick_foot_sign * (-m_params.stabilizing_point_y));
    m_support_point_trajectories->get("pos_y").addPoint(fix2, kick_foot_sign * (-m_params.stabilizing_point_y));
    m_support_point_trajectories->get("pos_y").addPoint(fix3, kick_foot_sign * (-m_params.stabilizing_point_y));
    m_support_point_trajectories->get("pos_y").addPoint(fix4, kick_foot_sign * (-m_params.stabilizing_point_y));
    m_support_point_trajectories->get("pos_y").addPoint(fix5, kick_foot_sign * (-m_params.stabilizing_point_y));
    m_support_point_trajectories->get("pos_y").addPoint(fix6, kick_foot_sign * (-m_params.stabilizing_point_y));
    m_support_point_trajectories->get("pos_y").addPoint(fix7, kick_foot_sign * (m_params.foot_distance / 2.0));
}

void DynupEngine::init_trajectories() {
    m_support_point_trajectories = Trajectories();

    m_support_point_trajectories->add("pos_x");
    m_support_point_trajectories->add("pos_y");

    m_flying_trajectories = Trajectories();

    m_flying_trajectories->add("pos_x");
    m_flying_trajectories->add("pos_y");
    m_flying_trajectories->add("pos_z");

    m_flying_trajectories->add("roll");
    m_flying_trajectories->add("pitch");
    m_flying_trajectories->add("yaw");
}

std::optional<std::pair<geometry_msgs::Vector3, geometry_msgs::Vector3>> DynupEngine::transform_goal(
        const std::string &support_foot_frame,
        const geometry_msgs::Vector3Stamped &ball_position,
        const geometry_msgs::Vector3Stamped &kick_movement) {

    /* Lookup transforms from goal frames to support_foot_frame */
    geometry_msgs::TransformStamped ball_position_transform;
    geometry_msgs::TransformStamped kick_movement_transform;
    try {
        ball_position_transform = m_tf_buffer.lookupTransform(support_foot_frame, ball_position.header.frame_id,
                                                              ros::Time(0), ros::Duration(1.0));
        kick_movement_transform = m_tf_buffer.lookupTransform(support_foot_frame, kick_movement.header.frame_id,
                                                              ros::Time(0), ros::Duration(1.0));
    } catch (tf2::TransformException &ex) { // TODO this exception is bullshit. It does not get thrown by lookupTransform
        ROS_ERROR("%s", ex.what());
        return std::nullopt;
    }

    /* Do transformation of goals into support_foot_frame with previously retrieved transform */
    geometry_msgs::Vector3Stamped transformed_ball_position;
    geometry_msgs::Vector3Stamped transformed_kick_movement;
    tf2::doTransform(ball_position, transformed_ball_position, ball_position_transform);
    tf2::doTransform(kick_movement, transformed_kick_movement, kick_movement_transform);

    return std::pair(transformed_ball_position.vector, transformed_kick_movement.vector);
}

tf2::Vector3 DynupEngine::calc_kick_windup_point() {
    tf2::Vector3 kick_movement = tf2::Vector3(m_kick_movement.x, m_kick_movement.y, m_params.foot_rise).normalize();
    kick_movement *= -m_params.kick_windup_distance;

    tf2::Vector3 goal_tf2;
    tf2::fromMsg(m_ball_position, goal_tf2);
    kick_movement += goal_tf2;

    return kick_movement;
}

bool DynupEngine::calc_is_left_foot_kicking(const geometry_msgs::Vector3Stamped &ball_position,
                                           const geometry_msgs::Vector3Stamped &kick_movement) {
    /* transform ball data into frame where we want to apply it */
    geometry_msgs::Vector3Stamped transformed_ball_position;
    m_tf_buffer.transform(ball_position, transformed_ball_position, "base_footprint", ros::Duration(0.2));

    /* check if ball is outside of an imaginary corridor */
    if (transformed_ball_position.vector.y > m_params.choose_foot_corridor_width / 2)
        return true;
    else if (transformed_ball_position.vector.y < -m_params.choose_foot_corridor_width / 2)
        return false;

    /* use the more fine grained angle based criterion */
    double angle = get_angular_difference(transformed_ball_position.vector, kick_movement.vector);

    ROS_INFO_STREAM("Choosing " << ((angle < 0) ? "left" : "right") << " foot to kick");

    return angle < 0;
}

double DynupEngine::calc_kick_foot_yaw() {
    geometry_msgs::Vector3 ahead;
    ahead.x = 1.0;
    ahead.y = 0.0;
    ahead.z = 0.0;
    double kick_direction_angle = get_angular_difference(ahead, m_kick_movement);
    if(kick_direction_angle > M_PI_4){
        return kick_direction_angle - M_PI_2;
    } 
    else if(kick_direction_angle < -M_PI_4){
        return kick_direction_angle + M_PI_2;
    }
    else {
        return kick_direction_angle;
    }
    
}

double DynupEngine::get_angular_difference(const geometry_msgs::Vector3 &vector1,
                                          const geometry_msgs::Vector3 &vector2) {
    double dot_product = vector1.x * vector2.x
            + vector1.y * vector2.y;
    double determinant = vector1.x * vector2.y
            - vector1.y * vector2.x;
    double angle = std::atan2(determinant, dot_product);
    return angle;
}

bool DynupEngine::is_left_kick() {
    return m_is_left_kick;
}

int DynupEngine::get_percent_done() const {
    double duration = m_params.move_trunk_time + m_params.raise_foot_time + m_params.move_to_ball_time +
                      m_params.kick_time + m_params.move_back_time + m_params.move_trunk_back_time  + m_params.lower_foot_time;
    return int(m_time / duration * 100);
}

void DynupEngine::set_params(KickParams params) {
    m_params = params;
}
