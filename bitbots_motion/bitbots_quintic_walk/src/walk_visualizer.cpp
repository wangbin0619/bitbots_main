#include "bitbots_quintic_walk/walk_visualizer.hpp"

namespace bitbots_quintic_walk {
WalkVisualizer::WalkVisualizer(rclcpp::Node::SharedPtr node, walking::Params::Node::Tf tf_config)
    : node_(node),
      tf_config_(tf_config),
      pub_debug_(node_->create_publisher<bitbots_quintic_walk::msg::WalkDebug>("walk_debug", 1)),
      pub_engine_debug_(node_->create_publisher<bitbots_quintic_walk::msg::WalkEngineDebug>("walk_engine_debug", 1)),
      pub_debug_marker_(node_->create_publisher<visualization_msgs::msg::Marker>("walk_debug_marker", 1)) {}

void WalkVisualizer::init(moveit::core::RobotModelPtr kinematic_model) { kinematic_model_ = kinematic_model; }

std::pair<bitbots_quintic_walk::msg::WalkEngineDebug, visualization_msgs::msg::MarkerArray>
WalkVisualizer::publishEngineDebug(WalkResponse response) {
  // Here we will convert the walk engine response to a various debug messages and RViz markers

  // Only do something if someone is listing
  if (pub_engine_debug_->get_subscription_count() == 0 && pub_debug_marker_->get_subscription_count() == 0) {
    return;
  }

  // Initialize output containers
  bitbots_quintic_walk::msg::WalkEngineDebug msg;
  visualization_msgs::msg::MarkerArray marker_array;

  // Copy some data into the debug message
  msg.is_left_support = response.is_left_support_foot;
  msg.is_double_support = response.is_double_support;
  msg.header.stamp = node_->now();
  msg.phase_time = response.phase;
  msg.traj_time = response.traj_time;
  // Copy walk engine state
  static const std::unordered_map<WalkState, std::string> state_string_mapping = {
      {WalkState::IDLE, "idle"},
      {WalkState::START_MOVEMENT, "start_movement"},
      {WalkState::START_STEP, "start_step"},
      {WalkState::WALKING, "walking"},
      {WalkState::PAUSED, "paused"},
      {WalkState::KICK, "kick"},
      {WalkState::STOP_STEP, "stop_step"},
      {WalkState::STOP_MOVEMENT, "stop_movement"}};
  msg.state.data = state_string_mapping.at(response.state);
  msg.state_number = static_cast<int>(response.state);

  // Define current support foot frame
  std::string current_support_frame;
  if (response.is_left_support_foot) {
    current_support_frame = tf_config_.l_sole_frame;
  } else {
    current_support_frame = tf_config_.r_sole_frame;
  }

  // Define colors based on current support state
  std_msgs::msg::ColorRGBA color;
  if (response.is_double_support) {
    color = BLUE;
  } else if (response.is_left_support_foot) {
    color = RED;
  } else {
    color = GREEN;
  }

  // Create placeholder floats
  double _1, _2;
  // Copy transform of the last footstep position (and orientation) to the debug message
  msg.footstep_last.x = response.support_to_last.getOrigin()[0];
  msg.footstep_last.y = response.support_to_last.getOrigin()[1];
  tf2::Matrix3x3(response.support_to_last.getRotation()).getRPY(_1, _2, msg.footstep_last.z);

  // Copy transform of the next footstep position (and orientation) to the debug message
  msg.footstep_next.x = response.support_to_next.getOrigin()[0];
  msg.footstep_next.y = response.support_to_next.getOrigin()[1];
  tf2::Matrix3x3(response.support_to_next.getRotation()).getRPY(_1, _2, msg.footstep_next.z);

  // Copy cartesian coordinates of the currently flying foot relative to the support foot to the debug message
  tf2::toMsg(response.support_foot_to_flying_foot, msg.fly_goal);
  // Create an additional marker for the flying foot goal
  marker_array.markers.push_back(createArrowMarker("engine_fly_goal", current_support_frame, msg.fly_goal, BLUE));

  // Copy the rotation of the flying foot relative to the support foot to the debug message
  tf2::Matrix3x3(response.support_foot_to_flying_foot.getRotation())
      .getRPY(msg.fly_euler.x, msg.fly_euler.y, msg.fly_euler.z);

  // Copy cartesian coordinates of the trunk goal relative to the support foot to the debug message
  tf2::toMsg(response.support_foot_to_trunk, msg.trunk_goal);
  // Create an additional marker for the trunk goal
  marker_array.markers.push_back(createArrowMarker("engine_trunk_goal", current_support_frame, msg.trunk_goal, color));

  // TODO check this!!!
  msg.trunk_goal_abs = msg.trunk_goal;
  if (msg.trunk_goal_abs.position.y > 0) {
    msg.trunk_goal_abs.position.y -= response.foot_distance / 2;
  } else {
    msg.trunk_goal_abs.position.y += response.foot_distance / 2;
  }

  tf2::Matrix3x3(response.support_foot_to_flying_foot.getRotation())
      .getRPY(msg.trunk_euler.x, msg.trunk_euler.y, msg.trunk_euler.z);

  // resulting trunk pose  TODO don't cheat here
  geometry_msgs::msg::Pose pose;
  geometry_msgs::msg::Point point;
  point.x = 0;
  point.y = 0;
  point.z = 0;
  pose.position = point;
  pose.orientation.x = 0;
  pose.orientation.y = 0;
  pose.orientation.z = 0;
  pose.orientation.w = 1;
  marker_array.markers.push_back(createArrowMarker("trunk_result", tf_config_.base_link_frame, pose, color));

  // TODO return marker array
  return {msg, marker_array};
}

void WalkVisualizer::publishIKDebug(WalkResponse response, moveit::core::RobotStatePtr current_state,
                                    bitbots_splines::JointGoals joint_goals) {
  // only do something if someone is listing
  if (pub_debug_->get_subscription_count() == 0 && pub_debug_marker_->get_subscription_count() == 0) {
    return;
  }
  bitbots_quintic_walk::msg::WalkDebug msg;

  tf2::Transform trunk_to_support_foot = response.support_foot_to_trunk.inverse();
  tf2::Transform trunk_to_flying_foot = trunk_to_support_foot * response.support_foot_to_flying_foot;

  // goals
  geometry_msgs::msg::Pose pose_support_foot_goal;
  tf2::toMsg(trunk_to_support_foot, pose_support_foot_goal);
  msg.support_foot_goal = pose_support_foot_goal;
  geometry_msgs::msg::Pose pose_fly_foot_goal;
  tf2::toMsg(trunk_to_flying_foot, pose_fly_foot_goal);
  msg.fly_foot_goal = pose_fly_foot_goal;
  if (response.is_left_support_foot) {
    msg.left_foot_goal = pose_support_foot_goal;
    msg.right_foot_goal = pose_fly_foot_goal;
  } else {
    msg.left_foot_goal = pose_fly_foot_goal;
    msg.right_foot_goal = pose_support_foot_goal;
  }
  createArrowMarker("engine_left_goal", tf_config_.base_link_frame, msg.left_foot_goal, GREEN);
  createArrowMarker("engine_right_goal", tf_config_.base_link_frame, msg.right_foot_goal, RED);

  // IK results
  moveit::core::RobotStatePtr goal_state;
  goal_state.reset(new moveit::core::RobotState(kinematic_model_));
  std::vector<std::string> names = joint_goals.first;
  std::vector<double> goals = joint_goals.second;
  for (size_t i = 0; i < names.size(); i++) {
    // besides its name, this method only changes a single joint position...
    goal_state->setJointPositions(names[i], &goals[i]);
  }

  goal_state->updateLinkTransforms();
  geometry_msgs::msg::Pose pose_left_result;
  tf2::convert(goal_state->getFrameTransform("l_sole"), pose_left_result);
  msg.left_foot_ik_result = pose_left_result;
  geometry_msgs::msg::Pose pose_right_result;
  tf2::convert(goal_state->getFrameTransform("r_sole"), pose_right_result);
  msg.right_foot_ik_result = pose_right_result;
  if (response.is_left_support_foot) {
    msg.support_foot_ik_result = pose_left_result;
    msg.fly_foot_ik_result = pose_right_result;
  } else {
    msg.support_foot_ik_result = pose_right_result;
    msg.fly_foot_ik_result = pose_left_result;
  }
  createArrowMarker("ik_left", tf_config_.base_link_frame, pose_left_result, GREEN);
  createArrowMarker("ik_right", tf_config_.base_link_frame, pose_right_result, RED);

  // IK offsets
  tf2::Vector3 support_off;
  tf2::Vector3 fly_off;
  tf2::Vector3 tf_vec_left;
  tf2::Vector3 tf_vec_right;
  Eigen::Vector3d l_transform = goal_state->getGlobalLinkTransform("l_sole").translation();
  Eigen::Vector3d r_transform = goal_state->getGlobalLinkTransform("r_sole").translation();
  tf2::convert(l_transform, tf_vec_left);
  tf2::convert(r_transform, tf_vec_right);
  geometry_msgs::msg::Vector3 vect_msg;
  if (response.is_left_support_foot) {
    support_off = trunk_to_support_foot.getOrigin() - tf_vec_left;
    fly_off = trunk_to_flying_foot.getOrigin() - tf_vec_right;
    vect_msg.x = support_off.x();
    vect_msg.y = support_off.y();
    vect_msg.z = support_off.z();
    msg.left_foot_ik_offset = vect_msg;
    vect_msg.x = fly_off.x();
    vect_msg.y = fly_off.y();
    vect_msg.z = fly_off.z();
    msg.right_foot_ik_offset = vect_msg;
  } else {
    support_off = trunk_to_support_foot.getOrigin() - tf_vec_right;
    fly_off = trunk_to_flying_foot.getOrigin() - tf_vec_left;
    vect_msg.x = fly_off.x();
    vect_msg.y = fly_off.y();
    vect_msg.z = fly_off.z();
    msg.left_foot_ik_offset = vect_msg;
    vect_msg.x = support_off.x();
    vect_msg.y = support_off.y();
    vect_msg.z = support_off.z();
    msg.right_foot_ik_offset = vect_msg;
  }
  vect_msg.x = support_off.x();
  vect_msg.y = support_off.y();
  vect_msg.z = support_off.z();
  msg.support_foot_ik_offset = vect_msg;
  vect_msg.x = fly_off.x();
  vect_msg.y = fly_off.y();
  vect_msg.z = fly_off.z();
  msg.fly_foot_ik_offset = vect_msg;

  // actual positions
  geometry_msgs::msg::Pose pose_left_actual;
  tf2::convert(current_state->getGlobalLinkTransform("l_sole"), pose_left_actual);
  msg.left_foot_position = pose_left_actual;
  geometry_msgs::msg::Pose pose_right_actual;
  tf2::convert(current_state->getGlobalLinkTransform("r_sole"), pose_right_actual);
  msg.right_foot_position = pose_right_actual;
  if (response.is_left_support_foot) {
    msg.support_foot_position = pose_left_actual;
    msg.fly_foot_position = pose_right_actual;
  } else {
    msg.support_foot_position = pose_right_actual;
    msg.fly_foot_position = pose_left_actual;
  }

  // actual offsets
  l_transform = current_state->getGlobalLinkTransform("l_sole").translation();
  r_transform = current_state->getGlobalLinkTransform("r_sole").translation();
  tf2::convert(l_transform, tf_vec_left);
  tf2::convert(r_transform, tf_vec_right);
  if (response.is_left_support_foot) {
    support_off = trunk_to_support_foot.getOrigin() - tf_vec_left;
    fly_off = trunk_to_flying_foot.getOrigin() - tf_vec_right;
    vect_msg.x = support_off.x();
    vect_msg.y = support_off.y();
    vect_msg.z = support_off.z();
    msg.left_foot_actual_offset = vect_msg;
    vect_msg.x = fly_off.x();
    vect_msg.y = fly_off.y();
    vect_msg.z = fly_off.z();
    msg.right_foot_actual_offset = vect_msg;
  } else {
    support_off = trunk_to_support_foot.getOrigin() - tf_vec_right;
    fly_off = trunk_to_flying_foot.getOrigin() - tf_vec_left;
    vect_msg.x = fly_off.x();
    vect_msg.y = fly_off.y();
    vect_msg.z = fly_off.z();
    msg.left_foot_actual_offset = vect_msg;
    vect_msg.x = support_off.x();
    vect_msg.y = support_off.y();
    vect_msg.z = support_off.z();
    msg.right_foot_actual_offset = vect_msg;
  }
  vect_msg.x = support_off.x();
  vect_msg.y = support_off.y();
  vect_msg.z = support_off.z();
  msg.support_foot_actual_offset = vect_msg;
  vect_msg.x = fly_off.x();
  vect_msg.y = fly_off.y();
  vect_msg.z = fly_off.z();
  msg.fly_foot_actual_offset = vect_msg;

  pub_debug_->publish(msg);
}

visualization_msgs::msg::Marker WalkVisualizer::createArrowMarker(const std::string &name_space,
                                                                  const std::string &frame,
                                                                  const geometry_msgs::msg::Pose &pose,
                                                                  const std_msgs::msg::ColorRGBA &color) {
  visualization_msgs::msg::Marker marker_msg;
  marker_msg.header.stamp = node_->now();
  marker_msg.header.frame_id = frame;

  marker_msg.type = marker_msg.ARROW;
  marker_msg.ns = name_space;
  marker_msg.action = marker_msg.ADD;
  marker_msg.pose = pose;
  marker_msg.color = color;

  geometry_msgs::msg::Vector3 scale;
  scale.x = 0.01;
  scale.y = 0.003;
  scale.z = 0.003;
  marker_msg.scale = scale;

  marker_msg.id = marker_id_;  // TODO use consistent marker ids
  marker_id_++;

  return marker_msg;
}

void WalkVisualizer::publishWalkMarkers(WalkResponse response) {
  // only do something if someone is listing
  if (pub_debug_marker_->get_subscription_count() == 0) {
    return;
  }
  // publish markers
  visualization_msgs::msg::Marker marker_msg;
  marker_msg.header.stamp = node_->now();
  if (response.is_left_support_foot) {
    marker_msg.header.frame_id = tf_config_.l_sole_frame;
  } else {
    marker_msg.header.frame_id = tf_config_.r_sole_frame;
  }
  marker_msg.type = marker_msg.CUBE;
  marker_msg.action = 0;
  marker_msg.lifetime = rclcpp::Duration::from_nanoseconds(0.0);
  geometry_msgs::msg::Vector3 scale;
  scale.x = 0.20;
  scale.y = 0.10;
  scale.z = 0.01;
  marker_msg.scale = scale;
  // last step
  marker_msg.ns = "last_step";
  marker_msg.id = 1;
  std_msgs::msg::ColorRGBA color;
  color.r = 0;
  color.g = 0;
  color.b = 0;
  color.a = 1;
  marker_msg.color = color;
  geometry_msgs::msg::Pose pose;
  tf2::Vector3 step_pos = response.support_to_last.getOrigin();
  geometry_msgs::msg::Point point;
  point.x = step_pos[0];
  point.y = step_pos[1];
  point.z = 0;
  pose.position = point;
  tf2::Quaternion q;
  q.setRPY(0.0, 0.0, step_pos[2]);
  tf2::convert(q, pose.orientation);
  marker_msg.pose = pose;
  pub_debug_marker_->publish(marker_msg);

  // last step center
  marker_msg.ns = "step_center";
  marker_msg.id = marker_id_;
  scale.x = 0.01;
  scale.y = 0.01;
  scale.z = 0.01;
  marker_msg.scale = scale;
  pub_debug_marker_->publish(marker_msg);

  // next step
  marker_msg.id = marker_id_;
  marker_msg.ns = "next_step";
  scale.x = 0.20;
  scale.y = 0.10;
  scale.z = 0.01;
  marker_msg.scale = scale;
  color.r = 1;
  color.g = 1;
  color.b = 1;
  color.a = 0.5;
  marker_msg.color = color;
  step_pos = response.support_to_next.getOrigin();
  point.x = step_pos[0];
  point.y = step_pos[1];
  pose.position = point;
  q.setRPY(0.0, 0.0, step_pos[2]);
  tf2::convert(q, pose.orientation);
  marker_msg.pose = pose;
  pub_debug_marker_->publish(marker_msg);

  marker_id_++;
}

}  // namespace bitbots_quintic_walk
