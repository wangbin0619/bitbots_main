#include "bitbots_dynamic_kick/kick_node.h"

#include <memory>

namespace bitbots_dynamic_kick {

KickNode::KickNode() :
    server_(node_handle_, "dynamic_kick", boost::bind(&KickNode::executeCb, this, _1), false),
    listener_(tf_buffer_),
    engine_(),
    visualizer_("/debug/dynamic_kick") {

  /* load MoveIt! model */
  robot_model_loader::RobotModelLoader robot_model_loader("/robot_description", false);
  robot_model_loader.loadKinematicsSolvers(std::make_shared<kinematics_plugin_loader::KinematicsPluginLoader>());
  robot_model::RobotModelPtr kinematic_model = robot_model_loader.getModel();
  if (!kinematic_model) {
    ROS_FATAL("No robot model loaded, killing dynamic kick.");
    exit(1);
  }
  stabilizer_.setRobotModel(kinematic_model);
  ik_.init(kinematic_model);

  joint_goal_publisher_ = node_handle_.advertise<bitbots_msgs::JointCommand>("kick_motor_goals", 1);
  support_foot_publisher_ = node_handle_.advertise<std_msgs::Char>("dynamic_kick_support_state", 1);
  cop_l_subscriber_ = node_handle_.subscribe("cop_l", 1, &KickNode::copLCallback, this);
  cop_r_subscriber_ = node_handle_.subscribe("cop_r", 1, &KickNode::copRCallback, this);
  server_.start();
}

void KickNode::copLCallback(const geometry_msgs::PointStamped &cop) {
  if (cop.header.frame_id!="l_sole") {
    ROS_ERROR_STREAM("cop_l not in l_sole frame! Stabilizing will not work.");
  }
  stabilizer_.m_cop_left = cop.point;
}

void KickNode::copRCallback(const geometry_msgs::PointStamped &cop) {
  if (cop.header.frame_id!="r_sole") {
    ROS_ERROR_STREAM("cop_r not in r_sole frame! Stabilizing will not work.");
  }
  stabilizer_.m_cop_right = cop.point;
}

void KickNode::reconfigureCallback(bitbots_dynamic_kick::DynamicKickConfig &config, uint32_t /* level */) {
  engine_rate_ = config.engine_rate;

  KickParams params = KickParams();
  params.foot_rise = config.foot_rise;
  params.foot_distance = config.foot_distance;
  params.kick_windup_distance = config.kick_windup_distance;
  params.move_trunk_time = config.move_trunk_time;
  params.raise_foot_time = config.raise_foot_time;
  params.move_to_ball_time = config.move_to_ball_time;
  params.kick_time = config.kick_time;
  params.move_back_time = config.move_back_time;
  params.lower_foot_time = config.lower_foot_time;
  params.move_trunk_back_time = config.move_trunk_back_time;
  params.stabilizing_point_x = config.stabilizing_point_x;
  params.stabilizing_point_y = config.stabilizing_point_y;
  params.choose_foot_corridor_width = config.choose_foot_corridor_width;
  engine_.setParams(params);

  stabilizer_.useMinimalDisplacement(config.minimal_displacement);
  stabilizer_.useStabilizing(config.stabilizing);
  stabilizer_.useCop(config.use_center_of_pressure);
  stabilizer_.setTrunkHeight(config.trunk_height);
  stabilizer_.setStabilizingWeight(config.stabilizing_weight);
  stabilizer_.setFlyingWeight(config.flying_weight);
  stabilizer_.setTrunkOrientationWeight(config.trunk_orientation_weight);
  stabilizer_.setTrunkHeightWeight(config.trunk_height_weight);
  stabilizer_.setPFactor(config.stabilizing_p_x, config.stabilizing_p_y);
  stabilizer_.setIFactor(config.stabilizing_i_x, config.stabilizing_i_y);
  stabilizer_.setDFactor(config.stabilizing_d_x, config.stabilizing_d_y);

  VisualizationParams viz_params = VisualizationParams();
  viz_params.force_enable = config.force_enable;
  viz_params.spline_smoothness = config.spline_smoothness;
  visualizer_.setParams(viz_params);
}

void KickNode::executeCb(const bitbots_msgs::KickGoalConstPtr &goal) {
  // TODO: maybe switch to goal callback to be able to reject goals properly
  ROS_INFO("Accepted new goal");
  engine_.reset();

  if (auto foot_poses = getFootPoses()) {

    visualizer_.displayReceivedGoal(goal);

    /* Set engines goal and start calculating */
    KickGoals goals;
    goals.ball_position = goal->ball_position;
    goals.header = goal->header;
    goals.kick_direction = goal->kick_direction;
    goals.kick_speed = goal->kick_speed;
    goals.r_foot_pose = foot_poses->first;
    goals.l_foot_pose = foot_poses->second;

    engine_.setGoals(goals);
    stabilizer_.reset();
    ik_.reset();
    visualizer_.displayWindupPoint(engine_.getWindupPoint(), (engine_.isLeftKick()) ? "r_sole" : "l_sole");
    visualizer_.displayFlyingSplines(engine_.getSplines(), (engine_.isLeftKick()) ? "r_sole" : "l_sole");
    loopEngine();

    /* Figure out the reason why loopEngine() returned and act accordingly */
    if (server_.isPreemptRequested()) {
      /* Confirm that we canceled the previous goal */
      ROS_INFO("Cancelled old goal");
      bitbots_msgs::KickResult result;
      result.result = bitbots_msgs::KickResult::ABORTED;
      server_.setPreempted(result);
    } else {
      /* Publish results */
      ROS_INFO("Done kicking ball");
      bitbots_msgs::KickResult result;
      result.result = bitbots_msgs::KickResult::SUCCESS;
      server_.setSucceeded(result);
    }

  } else {
    /* Feet positions were not successfully retrieved */
    bitbots_msgs::KickResult result;
    result.result = bitbots_msgs::KickResult::REJECTED;
    server_.setAborted(result, "Transformation of feet into base_link not possible");
  }
}

std::optional<std::pair<geometry_msgs::Pose, geometry_msgs::Pose>> KickNode::getFootPoses() {
  ros::Time time = ros::Time::now();

  /* Construct zero-positions for both feet in their respective local frames */
  geometry_msgs::PoseStamped r_foot_origin, l_foot_origin;
  r_foot_origin.header.frame_id = "r_sole";
  r_foot_origin.pose.orientation.w = 1;
  r_foot_origin.header.stamp = time;

  l_foot_origin.header.frame_id = "l_sole";
  l_foot_origin.pose.orientation.w = 1;
  l_foot_origin.header.stamp = time;

  /* Transform both feet poses into the other foot's frame */
  geometry_msgs::PoseStamped r_foot_transformed, l_foot_transformed;
  tf_buffer_.transform(r_foot_origin, r_foot_transformed, "l_sole",
                       ros::Duration(0.2)); // TODO lookup thrown exceptions in internet and catch
  tf_buffer_.transform(l_foot_origin, l_foot_transformed, "r_sole", ros::Duration(0.2));

  return std::pair(r_foot_transformed.pose, l_foot_transformed.pose);
}

void KickNode::loopEngine() {
  /* Do the loop as long as nothing cancels it */
  while (server_.isActive() && !server_.isPreemptRequested()) {
    KickPositions positions = engine_.update(1.0/engine_rate_);
    // TODO: should positions be an std::optional? how are errors represented?
    std::unique_ptr<bio_ik::BioIKKinematicsQueryOptions> ik_goals = stabilizer_.stabilize(positions);
    visualizer_
        .displayStabilizingPoint(stabilizer_.getStabilizingTarget(), positions.is_left_kick ? "r_sole" : "l_sole");
    bitbots_splines::JointGoals motor_goals = ik_.calculate(std::move(ik_goals));

    bitbots_msgs::KickFeedback feedback;
    feedback.percent_done = engine_.getPercentDone();
    feedback.chosen_foot = engine_.isLeftKick() ?
                           bitbots_msgs::KickFeedback::FOOT_LEFT : bitbots_msgs::KickFeedback::FOOT_RIGHT;
    server_.publishFeedback(feedback);
    publishGoals(motor_goals);

    publishSupportFoot(engine_.isLeftKick());

    if (feedback.percent_done==100) {
      break;
    }

    /* Let ROS do some important work of its own and sleep afterwards */
    ros::spinOnce();
    ros::Rate loop_rate(engine_rate_);
    loop_rate.sleep();
  }
}

void KickNode::publishGoals(const bitbots_splines::JointGoals &goals) {
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

void KickNode::publishSupportFoot(bool is_left_kick) {
  std_msgs::Char msg;
  msg.data = !is_left_kick ? 'l' : 'r';
  support_foot_publisher_.publish(msg);
}

}

int main(int argc, char *argv[]) {
  /* Setup ROS node */
  ros::init(argc, argv, "dynamic_kick");
  bitbots_dynamic_kick::KickNode node;

  /* Setup dynamic_reconfigure */
  dynamic_reconfigure::Server<bitbots_dynamic_kick::DynamicKickConfig> dyn_reconf_server;
  dyn_reconf_server.setCallback(boost::bind(&bitbots_dynamic_kick::KickNode::reconfigureCallback, &node, _1, _2));

  ros::spin();
}
