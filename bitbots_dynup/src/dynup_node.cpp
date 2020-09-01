#include "bitbots_dynup/dynup_node.h"

namespace bitbots_dynup {

DynUpNode::DynUpNode() :
    server_(node_handle_, "dynup", boost::bind(&DynUpNode::executeCb, this, _1), false),
    engine_(),
    visualizer_("/debug/dynup"),
    robot_model_loader_("/robot_description", false),
    listener_(tf_buffer_) {

  robot_model_loader_.loadKinematicsSolvers(std::make_shared<kinematics_plugin_loader::KinematicsPluginLoader>());
  robot_model::RobotModelPtr kinematic_model = robot_model_loader_.getModel();
  if (!kinematic_model) {
    ROS_FATAL("No robot model loaded, killing dynamic kick.");
    exit(1);
  }
  double arm_max_length = kinematic_model->getLinkModel("r_upper_arm")->getShapeExtentsAtOrigin().x() +
                          kinematic_model->getLinkModel("r_lower_arm")->getShapeExtentsAtOrigin().x();
  geometry_msgs::PoseStamped shoulder_origin, shoulder_tf;
  shoulder_origin.header.frame_id="r_upper_arm";
  tf_buffer_.transform(shoulder_origin, shoulder_tf, "base_link",
                         ros::Duration(0.2));                
  engine_.init(arm_max_length, shoulder_tf.pose.position.y, shoulder_tf.pose.position.z);
  stabilizer_.setRobotModel(kinematic_model);
  ik_.init(kinematic_model);
  stabilizer_.init(kinematic_model);

  joint_goal_publisher_ = node_handle_.advertise<bitbots_msgs::JointCommand>("animation_motor_goals", 1);
  debug_publisher_ = node_handle_.advertise<visualization_msgs::Marker>("debug_markers", 1);
  server_.start();
}

void DynUpNode::reconfigureCallback(bitbots_dynup::DynUpConfig &config, uint32_t level) {
  engine_rate_ = config.engine_rate;

  DynUpConfig params = config;
  //TODO set good default parameters

  engine_.setParams(params);

  stabilizer_.useMinimalDisplacement(config.minimal_displacement);
  stabilizer_.useStabilizing(config.stabilizing);
  stabilizer_.setStabilizingWeight(config.stabilizing_weight);

  ik_.useMinimalDisplacement(config.minimal_displacement);
  ik_.useStabilizing(config.stabilizing);

  VisualizationParams viz_params = VisualizationParams();
  viz_params.spline_smoothness = config.spline_smoothness;
  visualizer_.setParams(viz_params);
}

void DynUpNode::executeCb(const bitbots_msgs::DynUpGoalConstPtr &goal) {
  // TODO: maybe switch to goal callback to be able to reject goals properly
  ROS_INFO("Accepted new goal");
  engine_.reset();
  if (std::optional<std::tuple<geometry_msgs::Pose, geometry_msgs::Pose, geometry_msgs::Pose, geometry_msgs::Pose>> poses = getCurrentPoses()) {
    DynupRequest request;
    request.l_foot_pose = std::get<0>(poses.value());
    request.front = goal->front;
    request.r_foot_pose = std::get<1>(poses.value());
    request.l_hand_pose = std::get<2>(poses.value());
    request.r_hand_pose = std::get<3>(poses.value());
    engine_.setGoals(request);
    stabilizer_.reset();
    visualizer_.displaySplines(engine_.getRFootSplines(), "base_link");
    visualizer_.displaySplines(engine_.getLFootSplines(), "r_sole");
    visualizer_.displaySplines(engine_.getLHandSplines(), "base_link");
    visualizer_.displaySplines(engine_.getRHandSplines(), "base_link");
    loopEngine();
    bitbots_msgs::DynUpResult r;
    r.successful = true;
    server_.setSucceeded(r);
  } else {
    ROS_ERROR_STREAM("Could not determine positions! Aborting standup.");
    bitbots_msgs::DynUpResult r;
    r.successful = false;
    server_.setAborted(r);
  }
}

void DynUpNode::loopEngine() {
  int failed_tick_counter = 0;
  /* Do the loop as long as nothing cancels it */
  while (server_.isActive() && !server_.isPreemptRequested()) {
    DynupResponse response = engine_.update(1.0 / engine_rate_);
    DynupResponse stabilized_response = stabilizer_.stabilize(response, ros::Duration(1.0 / engine_rate_)); //TODO: Stabilizing has to be replaced!!!
    bitbots_splines::JointGoals goals = ik_.calculate(stabilized_response);
    bitbots_msgs::DynUpFeedback feedback;
    feedback.percent_done = engine_.getPercentDone();
    server_.publishFeedback(feedback);
    publishGoals(goals);
    if(goals.first.empty()) {
      failed_tick_counter++;
    }
    if (feedback.percent_done == 100) {
      ROS_ERROR("Completed dynup with %d failed ticks.", failed_tick_counter);
      break;
    }

    /* Let ROS do some important work of its own and sleep afterwards */
    ros::spinOnce();
    ros::Rate loop_rate(engine_rate_);
    loop_rate.sleep();
    engine_.publishDebug(debug_publisher_);
  }
}

std::optional<std::tuple<geometry_msgs::Pose, geometry_msgs::Pose, geometry_msgs::Pose, geometry_msgs::Pose>> DynUpNode::getCurrentPoses() {
  ros::Time time = ros::Time::now();

  /* Construct zero-positions for all poses in their respective local frames */
  geometry_msgs::PoseStamped l_foot_origin, r_foot_origin, l_hand_origin, r_hand_origin;
  l_foot_origin.header.frame_id = "l_sole";
  l_foot_origin.pose.orientation.w = 1;
  l_foot_origin.header.stamp = time;

  r_foot_origin.header.frame_id = "r_sole";
  r_foot_origin.pose.orientation.w = 1;
  r_foot_origin.header.stamp = time;

  l_hand_origin.header.frame_id = "l_wrist";
  l_hand_origin.pose.orientation.w = 1;
  l_hand_origin.header.stamp = time;

  r_hand_origin.header.frame_id = "r_wrist";
  r_hand_origin.pose.orientation.w = 1;
  r_hand_origin.header.stamp = time;

  /* Transform all poses into the right foot or base_link frame */
  geometry_msgs::PoseStamped l_foot_transformed, r_foot_transformed, l_hand_transformed, r_hand_transformed;
  try {
    tf_buffer_.transform(l_foot_origin, l_foot_transformed, "r_sole",
                         ros::Duration(0.2));
    tf_buffer_.transform(r_foot_origin, r_foot_transformed, "base_link", ros::Duration(0.2));
    tf_buffer_.transform(l_hand_origin, l_hand_transformed, "base_link", ros::Duration(0.2));
    tf_buffer_.transform(r_hand_origin, r_hand_transformed, "base_link", ros::Duration(0.2));
    return std::make_tuple(l_foot_transformed.pose, r_foot_transformed.pose, l_hand_transformed.pose, r_hand_transformed.pose);
  } catch (tf2::TransformException &) {
    return std::nullopt;
  }

}

void DynUpNode::publishGoals(const bitbots_splines::JointGoals &goals) {
  /* Construct JointCommand message */
  bitbots_msgs::JointCommand command;
  command.header.stamp = ros::Time::now();

  /*
   * Since our JointGoals type is a vector of strings
   *  combined with a vector of numbers (motor name -> target position)
   *  and bitbots_msgs::JointCommand needs both vectors as well,
   *  we can just assign them
   */
  command.joint_names = goals.first;
  command.positions = goals.second;

  /* And because we are setting position goals and not movement goals, these vectors are set to -1.0*/
  std::vector<double> vels(goals.first.size(), -1.0);
  std::vector<double> accs(goals.first.size(), -1.0);
  std::vector<double> pwms(goals.first.size(), -1.0);
  command.velocities = vels;
  command.accelerations = accs;
  command.max_currents = pwms;

  joint_goal_publisher_.publish(command);
}
void DynUpNode::publishSupportFoot(bool is_left_dyn_up) {
  //TODO: Currently not implemented
}

}

int main(int argc, char *argv[]) {
  /* Setup ROS node */
  ros::init(argc, argv, "dynup");
  bitbots_dynup::DynUpNode node;

  /* Setup dynamic_reconfigure */
  dynamic_reconfigure::Server<bitbots_dynup::DynUpConfig> dyn_reconf_server;
  dynamic_reconfigure::Server<bitbots_dynup::DynUpConfig>::CallbackType f;
  f = boost::bind(&bitbots_dynup::DynUpNode::reconfigureCallback, &node, _1, _2);
  dyn_reconf_server.setCallback(f);

  ROS_INFO("Initialized DynUp and waiting for actions");
  ros::spin();
}
