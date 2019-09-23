#include "bitbots_quintic_walk/walk_node.h"

namespace bitbots_quintic_walk {

WalkNode::WalkNode() :
    robot_model_loader_("/robot_description", false),
    walk_engine_() {
  // init variables
  robot_state_ = humanoid_league_msgs::RobotControlState::CONTROLABLE;
  current_request_.orders = tf2::Transform();
  odom_broadcaster_ = tf2_ros::TransformBroadcaster();

  // read config
  nh_.param<double>("engine_frequency", engine_frequency_, 100.0);
  nh_.param<bool>("/simulation_active", simulation_active_, false);
  nh_.param<bool>("/walking/publishOdomTF", publish_odom_tf_, false);

  /* init publisher and subscriber */
  command_msg_ = bitbots_msgs::JointCommand();
  pub_controller_command_ = nh_.advertise<bitbots_msgs::JointCommand>("walking_motor_goals", 1);
  odom_msg_ = nav_msgs::Odometry();
  pub_odometry_ = nh_.advertise<nav_msgs::Odometry>("walk_odometry", 1);
  pub_support_ = nh_.advertise<std_msgs::Char>("walk_support_state", 1);
  nh_.subscribe("cmd_vel", 1, &WalkNode::cmdVelCb, this,
                ros::TransportHints().tcpNoDelay());
  nh_.subscribe("robot_state", 1, &WalkNode::robStateCb, this,
                ros::TransportHints().tcpNoDelay());
  nh_.subscribe("joint_states", 1, &WalkNode::jointStateCb, this, ros::TransportHints().tcpNoDelay());
  nh_.subscribe("kick", 1, &WalkNode::kickCb, this, ros::TransportHints().tcpNoDelay());
  nh_.subscribe("imu/data", 1, &WalkNode::imuCb, this, ros::TransportHints().tcpNoDelay());
  nh_.subscribe("foot_pressure_filtered", 1, &WalkNode::pressureCb, this,
                ros::TransportHints().tcpNoDelay());
  nh_.subscribe("cop_l", 1, &WalkNode::copLCb, this, ros::TransportHints().tcpNoDelay());
  nh_.subscribe("cop_r", 1, &WalkNode::copRCb, this, ros::TransportHints().tcpNoDelay());


  //load MoveIt! model
  robot_model_loader_.loadKinematicsSolvers(
      kinematics_plugin_loader::KinematicsPluginLoaderPtr(
          new kinematics_plugin_loader::KinematicsPluginLoader()));
  kinematic_model_ = robot_model_loader_.getModel();
  if (!kinematic_model_) {
    ROS_FATAL("No robot model loaded, killing quintic walk.");
    exit(1);
  }
  //stabilizer_.setRobotModel(kinematic_model_);
  ik_.init(kinematic_model_);

  current_state_.reset(new robot_state::RobotState(kinematic_model_));
  current_state_->setToDefaultValues();

  first_run_ = true;

  std::shared_ptr<ros::NodeHandle> shared_nh(&nh_);
  visualizer_ = WalkVisualizer(shared_nh);

  // initialize dynamic-reconfigure
  server_.setCallback(boost::bind(&WalkNode::reconfCallback, this, _1, _2));

}

void WalkNode::run() {
  int odom_counter = 0;

  while (ros::ok()) {
    ros::Rate loop_rate(engine_frequency_);
    double dt = getTimeDelta();

    if (robot_state_==humanoid_league_msgs::RobotControlState::FALLING) {
      // the robot fell, we have to reset everything and do nothing else
      walk_engine_.reset();
    } else {
      // we don't want to walk, even if we have orders, if we are not in the right state
      /* Our robots will soon^TM be able to sit down and stand up autonomously, when sitting down the motors are
       * off but will turn on automatically which is why MOTOR_OFF is a valid walkable state. */
      // TODO Figure out a better way than having integration knowledge that HCM will play an animation to stand up
      current_request_.walkable_state = robot_state_==humanoid_league_msgs::RobotControlState::CONTROLABLE ||
          robot_state_==humanoid_league_msgs::RobotControlState::WALKING
          || robot_state_==humanoid_league_msgs::RobotControlState::MOTOR_OFF;
      // update walk engine response
      walk_engine_.setGoals(current_request_);
      WalkResponse response = walk_engine_.update(dt);
      // only calculate joint goals from this if the engine is not idle
      if (walk_engine_.getState()!="idle") { //todo
        calculateAndPublishJointGoals(response);
      }
    }

    // publish odometry
    odom_counter++;
    if (odom_counter > odom_pub_factor_) {
      publishOdometry();
      odom_counter = 0;
    }
    ros::spinOnce();
    loop_rate.sleep();
  }
}

void WalkNode::calculateAndPublishJointGoals(WalkResponse response) {
  // get bioIk goals from stabilizer
  std::unique_ptr<bio_ik::BioIKKinematicsQueryOptions> ik_goals = stabilizer_.stabilize(response);

  // compute motor goals from IK
  bitbots_splines::JointGoals motor_goals = ik_.calculate(std::move(ik_goals));

  // publish them
  publishGoals(motor_goals);

  // publish current support state
  std_msgs::Char support_state;
  if (walk_engine_.isDoubleSupport()) {
    support_state.data = 'd';
  } else if (walk_engine_.isLeftSupport()) {
    support_state.data = 'l';
  } else {
    support_state.data = 'r';
  }
  pub_support_.publish(support_state);

  // publish debug information
  if (debug_active_) {
    visualizer_.publishEngineDebug(response);
    //todo
    //visualizer_.publishIKDebug(response, current_state_, goal_state_, trunk_to_support_foot_goal, trunk_to_flying_foot_goal);
    visualizer_.publishWalkMarkers(response);
  }
}

double WalkNode::getTimeDelta() {
  // compute time delta depended if we are currently in simulation or reality
  double dt;
  double current_ros_time = ros::Time::now().toSec();
  dt = current_ros_time - last_ros_update_time_;
  if (dt==0) {
    ROS_WARN("dt was 0");
    dt = 0.001;
  }
  last_ros_update_time_ = current_ros_time;

  // time is wrong when we run it for the first time
  if (first_run_) {
    first_run_ = false;
    dt = 0.0001;
  }
  return dt;
}

void WalkNode::cmdVelCb(const geometry_msgs::Twist msg) {
  // we use only 3 values from the twist messages, as the robot is not capable of jumping or spinning around its
  // other axis.

  // the engine expects orders in [m] not [m/s]. We have to compute by dividing by step frequency which is a double step
  // factor 2 since the order distance is only for a single step, not double step
  double factor = (1.0/(walk_engine_.getFreq()))/2.0;
  tf2::Vector3 orders = {msg.linear.x*factor, msg.linear.y*factor, msg.angular.z*factor};

  // the orders should not extend beyond a maximal step size
  for (int i = 0; i < 3; i++) {
    orders[i] = std::max(std::min(orders[i], max_step_[i]), max_step_[i]*-1);
  }
  // translational orders (x+y) should not exceed combined limit. scale if necessary
  if (max_step_xy_!=0) {
    double scaling_factor = (orders[0] + orders[1])/max_step_xy_;
    for (int i = 0; i < 2; i++) {
      orders[i] = orders[i]/std::max(scaling_factor, 1.0);
    }
  }

  // warn user that speed was limited
  if (msg.linear.x*factor!=orders[0] ||
      msg.linear.y*factor!=orders[1] ||
      msg.angular.z*factor!=orders[2]) {
    ROS_WARN(
        "Speed command was x: %.2f y: %.2f z: %.2f xy: %.2f but maximum is x: %.2f y: %.2f z: %.2f xy: %.2f",
        msg.linear.x, msg.linear.y, msg.angular.z, msg.linear.x + msg.linear.y, max_step_[0]/factor,
        max_step_[1]/factor, max_step_[2]/factor, max_step_xy_/factor);
  }

  // The result is the transform where the engine should place it next foot, we only use x,y and yaw
  current_request_.orders.setOrigin({orders[0], orders[1], 0});
  tf2::Quaternion quat;
  quat.setRPY(0, 0, orders[2]);
  current_request_.orders.setRotation(quat);
}

void WalkNode::imuCb(const sensor_msgs::Imu msg) {
  if (imu_active_) {
    // the incoming geometry_msgs::Quaternion is transformed to a tf2::Quaterion
    tf2::Quaternion quat;
    tf2::convert(msg.orientation, quat);

    // the tf2::Quaternion has a method to access roll pitch and yaw
    double roll, pitch, yaw;
    tf2::Matrix3x3(quat).getRPY(roll, pitch, yaw);

    // compute the pitch offset to the currently wanted pitch of the engine
    double wanted_pitch = walk_engine_.getWantedTrunkPitch();

    pitch = pitch + wanted_pitch;

    // get angular velocities
    double roll_vel = msg.angular_velocity.x;
    double pitch_vel = msg.angular_velocity.y;
    if (abs(roll) > imu_roll_threshold_ || abs(pitch) > imu_pitch_threshold_ ||
        abs(pitch_vel) > imu_pitch_vel_threshold_ || abs(roll_vel) > imu_roll_vel_threshold_) {
      walk_engine_.requestPause();
      if (abs(roll) > imu_roll_threshold_) {
        ROS_WARN("imu roll angle stop");
      } else if (abs(pitch) > imu_pitch_threshold_) {
        ROS_WARN("imu pitch angle stop");
      } else if (abs(pitch_vel) > imu_pitch_vel_threshold_) {
        ROS_WARN("imu roll vel stop");
      } else {
        ROS_WARN("imu pitch vel stop");
      }
    }
  }
}

void WalkNode::pressureCb(
    const bitbots_msgs::FootPressure msg) { // TODO Remove this method since cop_cb is now used
  // we just want to look at the support foot. choose the 4 values from the message accordingly
  // s = support, n = not support, i = inside, o = outside, f = front, b = back
  double sob;
  double sof;
  double sif;
  double sib;

  double nob;
  double nof;
  double nif;
  double nib;

  if (walk_engine_.isLeftSupport()) {
    sob = msg.l_l_b;
    sof = msg.l_l_f;
    sif = msg.l_r_f;
    sib = msg.l_r_b;

    nib = msg.r_l_b;
    nif = msg.r_l_f;
    nof = msg.r_r_f;
    nob = msg.r_r_b;
  } else {
    sib = msg.r_l_b;
    sif = msg.r_l_f;
    sof = msg.r_r_f;
    sob = msg.r_r_b;

    nob = msg.l_l_b;
    nof = msg.l_l_f;
    nif = msg.l_r_f;
    nib = msg.l_r_b;
  }

  // sum to get overall pressure on not support foot
  double n_sum = nob + nof + nif + nib;

  // ratios between pressures to get relative position of CoP
  double s_io_ratio = 100;
  if (sof + sob!=0) {
    s_io_ratio = (sif + sib)/(sof + sob);
    if (s_io_ratio==0) {
      s_io_ratio = 100;
    }
  }
  double s_fb_ratio = 100;
  if (sib + sob!=0) {
    s_fb_ratio = (sif + sof)/(sib + sob);
    if (s_fb_ratio==0) {
      s_fb_ratio = 100;
    }
  }

  // check for early step end
  // phase has to be far enough (almost at end of step) to have right foot lifted
  // foot has to have ground contact
  double phase = walk_engine_.getPhase();
  if (phase_reset_active_ && ((phase > 0.5 - phase_reset_phase_ && phase < 0.5) || (phase > 1 - phase_reset_phase_)) &&
      n_sum > ground_min_pressure_) {
    ROS_WARN("Phase resetted!");
    walk_engine_.endStep();
  }

  // check if robot is unstable and should pause
  // this is true if the robot is falling to the outside or to front or back
  if (pressure_stop_active_ && (s_io_ratio > io_pressure_threshold_ || 1/s_io_ratio > io_pressure_threshold_ ||
      1/s_fb_ratio > fb_pressure_threshold_ || s_fb_ratio > fb_pressure_threshold_)) {
    walk_engine_.requestPause();

    //TODO this is debug
    if (s_io_ratio > io_pressure_threshold_ || 1/s_io_ratio > io_pressure_threshold_) {
      ROS_WARN("CoP io stop!");
    } else {
      ROS_WARN("CoP fb stop!");
    }
  }

  // decide which CoP
  geometry_msgs::PointStamped cop;
  if (walk_engine_.isLeftSupport()) {
    cop = cop_l_;
  } else {
    cop = cop_r_;
  }

  if (cop_stop_active_ && (abs(cop.point.x) > cop_x_threshold_ || abs(cop.point.y) > cop_y_threshold_)) {
    walk_engine_.requestPause();
    if (abs(cop.point.x) > cop_x_threshold_) {
      ROS_WARN("cop x stop");
    } else {
      ROS_WARN("cop y stop");
    }
  }

}

void WalkNode::robStateCb(const humanoid_league_msgs::RobotControlState msg) {
  robot_state_ = msg.state;
}

void WalkNode::jointStateCb(const sensor_msgs::JointState msg) {
  std::vector<std::string> names_vec = msg.name;
  std::string *names = names_vec.data();

  current_state_->setJointPositions(*names, msg.position.data());
}

void WalkNode::kickCb(const std_msgs::BoolConstPtr msg) {
  walk_engine_.requestKick(msg->data);
}

void WalkNode::copLCb(const geometry_msgs::PointStamped msg) {
  cop_l_ = msg;
}

void WalkNode::copRCb(const geometry_msgs::PointStamped msg) {
  cop_r_ = msg;
}

void
WalkNode::reconfCallback(bitbots_quintic_walk::bitbots_quintic_walk_paramsConfig &config,
                         uint32_t level) {
  params_ = config;

  // todo
  // bio_ik_solver_.set_bioIK_timeout(config.bio_ik_time);

  debug_active_ = config.debug_active;
  engine_frequency_ = config.engine_freq;
  odom_pub_factor_ = config.odom_pub_factor;

  max_step_[0] = config.max_step_x;
  max_step_[1] = config.max_step_y;
  max_step_[2] = config.max_step_z;
  max_step_xy_ = config.max_step_xy;

  imu_active_ = config.imu_active;
  imu_pitch_threshold_ = config.imu_pitch_threshold;
  imu_roll_threshold_ = config.imu_roll_threshold;
  imu_pitch_vel_threshold_ = config.imu_pitch_vel_threshold;
  imu_roll_vel_threshold_ = config.imu_roll_vel_threshold;

  phase_reset_active_ = config.phase_reset_active;
  phase_reset_phase_ = config.phase_reset_phase;
  ground_min_pressure_ = config.ground_min_pressure;
  cop_stop_active_ = config.cop_stop_active;
  cop_x_threshold_ = config.cop_x_threshold;
  cop_y_threshold_ = config.cop_y_threshold;
  pressure_stop_active_ = config.pressure_stop_active;
  io_pressure_threshold_ = config.io_pressure_threshold;
  fb_pressure_threshold_ = config.fb_pressure_threshold;
  params_.pause_duration = config.pause_duration;
  walk_engine_.setPauseDuration(params_.pause_duration);
}

//todo this is the same method as in kick, maybe put it into a utility class
void WalkNode::publishGoals(const bitbots_splines::JointGoals &goals) {
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

  pub_controller_command_.publish(command);
}

void WalkNode::publishOdometry() {
  // transformation from support leg to trunk
  //todo
  /*
  Eigen::Isometry3d trunk_to_support;
  if (walk_engine_.getFootstep().isLeftSupport()) {
    trunk_to_support = goal_state_->getGlobalLinkTransform("l_sole");
  } else {
    trunk_to_support = goal_state_->getGlobalLinkTransform("r_sole");
  }
  Eigen::Isometry3d support_to_trunk = trunk_to_support.inverse();
  tf2::Transform tf_support_to_trunk;
  tf2::convert(support_to_trunk, tf_support_to_trunk);

  // odometry to trunk is transform to support foot * transform from support to trunk
  tf2::Transform support_foot_tf;
  if (walk_engine_.getFootstep().isLeftSupport()) {
    support_foot_tf = walk_engine_.getFootstep().getLeft();
  } else {
    support_foot_tf = walk_engine_.getFootstep().getRight();
  }

  tf2::Transform odom_to_trunk = support_foot_tf*tf_support_to_trunk;
  tf2::Vector3 pos = odom_to_trunk.getOrigin();
  geometry_msgs::Quaternion quat_msg;

  tf2::convert(odom_to_trunk.getRotation().normalize(), quat_msg);

  ros::Time current_time = ros::Time::now();

  if (publish_odom_tf_) {
    odom_trans_ = geometry_msgs::TransformStamped();
    odom_trans_.header.stamp = current_time;
    odom_trans_.header.frame_id = "odom";
    odom_trans_.child_frame_id = "base_link";

    odom_trans_.transform.translation.x = pos[0];
    odom_trans_.transform.translation.y = pos[1];
    odom_trans_.transform.translation.z = pos[2];
    odom_trans_.transform.rotation = quat_msg;

    //send the transform
    odom_broadcaster_.sendTransform(odom_trans_);
  }

  // send the odometry also as message
  odom_msg_.header.stamp = current_time;
  odom_msg_.header.frame_id = "odom";
  odom_msg_.child_frame_id = "base_link";
  odom_msg_.pose.pose.position.x = pos[0];
  odom_msg_.pose.pose.position.y = pos[1];
  odom_msg_.pose.pose.position.z = pos[2];

  odom_msg_.pose.pose.orientation = quat_msg;
  geometry_msgs::Twist twist;

  twist.linear.x = current_request_.orders.getOrigin()[0]*params_.freq*2;
  twist.linear.y = current_request_.orders.getOrigin()[1]*params_.freq*2;
  double roll, pitch, yaw;
  tf2::Matrix3x3(current_request_.orders.getRotation()).getRPY(roll, pitch, yaw);
  twist.angular.z = yaw*params_.freq*2;

  odom_msg_.twist.twist = twist;
  pub_odometry_.publish(odom_msg_);
   */
}

void WalkNode::initializeEngine() {
  walk_engine_.reset();
}

}

int main(int argc, char **argv) {
  ros::init(argc, argv, "quintic_walking");
  // init node
  bitbots_quintic_walk::WalkNode node;

  // run the node
  node.initializeEngine();
  node.run();
}
