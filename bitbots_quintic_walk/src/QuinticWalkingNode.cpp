#include "bitbots_quintic_walk/QuinticWalkingNode.hpp"


namespace bitbots_quintic_walk {

    QuinticWalkingNode::QuinticWalkingNode() :
            _robot_model_loader("/robot_description", false) {
        // init variables
        _robotState = humanoid_league_msgs::RobotControlState::CONTROLABLE;
        _walkEngine = bitbots_quintic_walk::QuinticWalk();
        _isLeftSupport = true;
        _currentOrders[0] = 0.0;
        _currentOrders[1] = 0.0;
        _currentOrders[2] = 0.0;

        _marker_id = 1;
        _odom_broadcaster = tf2_ros::TransformBroadcaster();

        // read config
        _nh.param<double>("engineFrequency", _engineFrequency, 100.0);
        _nh.param<bool>("/simulation_active", _simulation_active, false);
        _nh.param<bool>("/walking/publishOdomTF", _publishOdomTF, false);

        /* init publisher and subscriber */
        _command_msg = bitbots_msgs::JointCommand();
        _pubControllerCommand = _nh.advertise<bitbots_msgs::JointCommand>("walking_motor_goals", 1);
        _odom_msg = nav_msgs::Odometry();
        _pubOdometry = _nh.advertise<nav_msgs::Odometry>("walk_odometry", 1);
        _pubSupport = _nh.advertise<std_msgs::Char>("walk_support_state", 1);
        _subCmdVel = _nh.subscribe("cmd_vel", 1, &QuinticWalkingNode::cmdVelCb, this,
                                   ros::TransportHints().tcpNoDelay());
        _subRobState = _nh.subscribe("robot_state", 1, &QuinticWalkingNode::robStateCb, this,
                                     ros::TransportHints().tcpNoDelay());
        //todo not really needed
        //_subJointStates = _nh.subscribe("joint_states", 1, &QuinticWalkingNode::jointStateCb, this, ros::TransportHints().tcpNoDelay());
        _subKick = _nh.subscribe("kick", 1, &QuinticWalkingNode::kickCb, this, ros::TransportHints().tcpNoDelay());
        _subImu = _nh.subscribe("imu/data", 1, &QuinticWalkingNode::imuCb, this, ros::TransportHints().tcpNoDelay());
        _subPressure = _nh.subscribe("foot_pressure_filtered", 1, &QuinticWalkingNode::pressureCb, this,
                                     ros::TransportHints().tcpNoDelay());
        _subCopL = _nh.subscribe("cop_l", 1, &QuinticWalkingNode::cop_l_cb, this, ros::TransportHints().tcpNoDelay());
        _subCopR = _nh.subscribe("cop_r", 1, &QuinticWalkingNode::cop_r_cb, this, ros::TransportHints().tcpNoDelay());


        /* debug publisher */
        _pubDebug = _nh.advertise<bitbots_quintic_walk::WalkingDebug>("walk_debug", 1);
        _pubDebugMarker = _nh.advertise<visualization_msgs::Marker>("walk_debug_marker", 1);

        //load MoveIt! model
        _robot_model_loader.loadKinematicsSolvers(
                kinematics_plugin_loader::KinematicsPluginLoaderPtr(
                        new kinematics_plugin_loader::KinematicsPluginLoader()));
        _kinematic_model = _robot_model_loader.getModel();
        _all_joints_group = _kinematic_model->getJointModelGroup("All");
        _legs_joints_group = _kinematic_model->getJointModelGroup("Legs");
        _lleg_joints_group = _kinematic_model->getJointModelGroup("LeftLeg");
        _rleg_joints_group = _kinematic_model->getJointModelGroup("RightLeg");
        _goal_state.reset(new robot_state::RobotState(_kinematic_model));
        _goal_state->setToDefaultValues();
        // we have to set some good initial position in the goal state, since we are using a gradient
        // based method. Otherwise, the first step will be not correct
        std::vector<std::string> names_vec = {"LHipPitch", "LKnee", "LAnklePitch", "RHipPitch", "RKnee", "RAnklePitch"};
        std::vector<double> pos_vec = {0.7, -1.0, -0.4, -0.7, 1.0, 0.4};
        for (int i = 0; i < names_vec.size(); i++) {
            // besides its name, this method only changes a single joint position...
            _goal_state->setJointPositions(names_vec[i], &pos_vec[i]);
        }

        _current_state.reset(new robot_state::RobotState(_kinematic_model));
        _current_state->setToDefaultValues();

        // initilize IK solver
        _bioIK_solver = bitbots_ik::BioIKSolver(*_all_joints_group, *_lleg_joints_group, *_rleg_joints_group);
        _bioIK_solver.set_use_approximate(true);

        _first_run = true;

        // initialize dynamic-reconfigure
        _server.setCallback(boost::bind(&QuinticWalkingNode::reconf_callback, this, _1, _2));
    }


    void QuinticWalkingNode::run() {
        int odom_counter = 0;

        while (ros::ok()) {
            ros::Rate loopRate(_engineFrequency);
            double dt = getTimeDelta();

            if (_robotState == humanoid_league_msgs::RobotControlState::FALLING) {
                // the robot fell, we have to reset everything and do nothing else
                _walkEngine.reset();
            } else {
                // we don't want to walk, even if we have orders, if we are not in the right state
                /* Our robots will soon^TM be able to sit down and stand up autonomously, when sitting down the motors are
                 * off but will turn on automatically which is why MOTOR_OFF is a valid walkable state. */
                // TODO Figure out a better way than having integration knowledge that HCM will play an animation to stand up
                bool walkableState = _robotState == humanoid_league_msgs::RobotControlState::CONTROLABLE ||
                                     _robotState == humanoid_league_msgs::RobotControlState::WALKING
                                     || _robotState == humanoid_league_msgs::RobotControlState::MOTOR_OFF;
                // see if the walk engine has new goals for us
                bool newGoals = _walkEngine.updateState(dt, _currentOrders, walkableState);
                if (_walkEngine.getState() != "idle") { //todo
                    calculateJointGoals();
                }
            }

            // publish odometry
            odom_counter++;
            if (odom_counter > _odomPubFactor) {
                publishOdometry();
                odom_counter = 0;
            }
            ros::spinOnce();
            loopRate.sleep();
        }
    }

    void QuinticWalkingNode::calculateJointGoals() {
        // read the cartesian positions and orientations for trunk and fly foot
        _walkEngine.computeCartesianPosition(_trunkPos, _trunkAxis, _footPos, _footAxis, _isLeftSupport);

        // change goals from support foot based coordinate system to trunk based coordinate system
        tf2::Vector3 tf_vec;
        tf2::convert(_trunkPos, tf_vec);
        tf2::Quaternion tf_quat;
        tf_quat.setRPY(_trunkAxis[0], _trunkAxis[1], _trunkAxis[2]);
        tf_quat.normalize();
        tf2::Transform support_foot_to_trunk(tf_quat, tf_vec);
        tf2::Transform trunk_to_support_foot_goal = support_foot_to_trunk.inverse();

        tf2::convert(_footPos, tf_vec);
        tf_quat.setRPY(_footAxis[0], _footAxis[1], _footAxis[2]);
        tf_quat.normalize();
        tf2::Transform support_to_flying_foot(tf_quat, tf_vec);
        tf2::Transform trunk_to_flying_foot_goal = trunk_to_support_foot_goal * support_to_flying_foot;

        // call ik solver
        bool success = _bioIK_solver.solve(trunk_to_support_foot_goal, trunk_to_flying_foot_goal,
                                           _walkEngine.getFootstep().isLeftSupport(), _goal_state);

        // publish goals if sucessfull
        if (success) {
            std::vector<std::string> joint_names = _legs_joints_group->getActiveJointModelNames();
            std::vector<double> joint_goals;
            _goal_state->copyJointGroupPositions(_legs_joints_group, joint_goals);
            publishControllerCommands(joint_names, joint_goals);
        }

        // publish current support state
        std_msgs::Char support_state;
        if (_walkEngine.isDoubleSupport()) {
            support_state.data = 'd';
        } else if (_walkEngine.isLeftSupport()) {
            support_state.data = 'l';
        } else {
            support_state.data = 'r';
        }
        _pubSupport.publish(support_state);

        // publish debug information
        if (_debugActive) {
            publishDebug(trunk_to_support_foot_goal, trunk_to_flying_foot_goal);
            publishMarkers();
        }

    }

    double QuinticWalkingNode::getTimeDelta() {
        // compute time delta depended if we are currently in simulation or reality
        double dt;
        if (!_simulation_active) {
            std::chrono::time_point<std::chrono::steady_clock> current_time = std::chrono::steady_clock::now();
            // only take real time difference if walking was not stopped before
            // using c++ time since it is more performant than ros time. We only need a local difference, so it doesnt matter as long as we are not simulating
            auto time_diff_ms = std::chrono::duration_cast<std::chrono::milliseconds>(current_time - _last_update_time);
            dt = time_diff_ms.count() / 1000.0;
            if (dt == 0) {
                ROS_WARN("dt was 0");
                dt = 0.001;
            }
            _last_update_time = current_time;
        } else {
            ROS_WARN_ONCE("Simulation active, using ROS time");
            // use ros time for simulation
            double current_ros_time = ros::Time::now().toSec();
            dt = current_ros_time - _last_ros_update_time;
            _last_ros_update_time = current_ros_time;
        }
        // time is wrong when we run it for the first time
        if (_first_run) {
            _first_run = false;
            dt = 0.0001;
        }
        return dt;
    }

    void QuinticWalkingNode::cmdVelCb(const geometry_msgs::Twist msg) {
        // we use only 3 values from the twist messages, as the robot is not capable of jumping or spinning around its
        // other axis.

        // the engine expects orders in [m] not [m/s]. We have to compute by dividing by step frequency which is a double step
        // factor 2 since the order distance is only for a single step, not double step
        double factor = (1.0 / (_params.freq)) / 2.0;
        _currentOrders = {msg.linear.x * factor, msg.linear.y * factor, msg.angular.z * factor};

        // the orders should not extend beyond a maximal step size
        for (int i = 0; i < 3; i++) {
            _currentOrders[i] = std::max(std::min(_currentOrders[i], _max_step[i]), _max_step[i] * -1);
        }
        // translational orders (x+y) should not exceed combined limit. scale if necessary
        if (_max_step_xy != 0) {
            double scaling_factor = (_currentOrders[0] + _currentOrders[1]) / _max_step_xy;
            for (int i = 0; i < 2; i++) {
                _currentOrders[i] = _currentOrders[i] / std::max(scaling_factor, 1.0);
            }
        }

        // warn user that speed was limited
        if (msg.linear.x * factor != _currentOrders[0] ||
            msg.linear.y * factor != _currentOrders[1] ||
            msg.angular.z * factor != _currentOrders[2]) {
            ROS_WARN(
                    "Speed command was x: %.2f y: %.2f z: %.2f xy: %.2f but maximum is x: %.2f y: %.2f z: %.2f xy: %.2f",
                    msg.linear.x, msg.linear.y, msg.angular.z, msg.linear.x + msg.linear.y, _max_step[0] / factor,
                    _max_step[1] / factor, _max_step[2] / factor, _max_step_xy / factor);
        }
    }

    void QuinticWalkingNode::imuCb(const sensor_msgs::Imu msg) {
        if (_imuActive) {
            // the incoming geometry_msgs::Quaternion is transformed to a tf2::Quaterion
            tf2::Quaternion quat;
            tf2::convert(msg.orientation, quat);

            // the tf2::Quaternion has a method to acess roll pitch and yaw
            double roll, pitch, yaw;
            tf2::Matrix3x3(quat).getRPY(roll, pitch, yaw);

            // compute the pitch offset to the currently wanted pitch of the engine
            double wanted_pitch =
                    _params.trunkPitch + _params.trunkPitchPCoefForward * _walkEngine.getFootstep().getNext().x()
                    + _params.trunkPitchPCoefTurn * fabs(_walkEngine.getFootstep().getNext().z());
            pitch = pitch + wanted_pitch;

            // get angular velocities
            double roll_vel = msg.angular_velocity.x;
            double pitch_vel = msg.angular_velocity.y;
            if (abs(roll) > _imu_roll_threshold || abs(pitch) > _imu_pitch_threshold ||
                abs(pitch_vel) > _imu_pitch_vel_threshold || abs(roll_vel) > _imu_roll_vel_threshold) {
                _walkEngine.requestPause();
                if (abs(roll) > _imu_roll_threshold) {
                    ROS_WARN("imu roll angle stop");
                } else if (abs(pitch) > _imu_pitch_threshold) {
                    ROS_WARN("imu pitch angle stop");
                } else if (abs(pitch_vel) > _imu_pitch_vel_threshold) {
                    ROS_WARN("imu roll vel stop");
                } else {
                    ROS_WARN("imu pitch vel stop");
                }
            }
        }
    }

    void QuinticWalkingNode::pressureCb(
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

        if (_walkEngine.isLeftSupport()) {
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

        // sum to get overall pressure on foot
        double s_sum = sob + sof + sif + sib;
        double n_sum = nob + nof + nif + nib;

        // ratios between pressures to get relative position of CoP
        double s_io_ratio = 100;
        if (sof + sob != 0) {
            s_io_ratio = (sif + sib) / (sof + sob);
            if (s_io_ratio == 0) {
                s_io_ratio = 100;
            }
        }
        double s_fb_ratio = 100;
        if (sib + sob != 0) {
            s_fb_ratio = (sif + sof) / (sib + sob);
            if (s_fb_ratio == 0) {
                s_fb_ratio = 100;
            }
        }

        // check for early step end
        // phase has to be far enough (almost at end of step) to have right foot lifted
        // foot has to have ground contact
        double phase = _walkEngine.getPhase();
        if (_phaseResetActive && ((phase > 0.5 - _phaseResetPhase && phase < 0.5) || (phase > 1 - _phaseResetPhase)) &&
            n_sum > _groundMinPressure) {
            ROS_WARN("Phase resetted!");
            _walkEngine.endStep();
        }

        // check if robot is unstable and should pause
        // this is true if the robot is falling to the outside or to front or back
        if (_pressureStopActive && (s_io_ratio > _ioPressureThreshold || 1 / s_io_ratio > _ioPressureThreshold ||
                                    1 / s_fb_ratio > _fbPressureThreshold || s_fb_ratio > _fbPressureThreshold)) {
            _walkEngine.requestPause();

            //TODO this is debug
            if (s_io_ratio > _ioPressureThreshold || 1 / s_io_ratio > _ioPressureThreshold) {
                ROS_WARN("CoP io stop!");
            } else {
                ROS_WARN("CoP fb stop!");
            }
        }

        // decide which CoP
        geometry_msgs::PointStamped cop;
        if (_walkEngine.isLeftSupport()) {
            cop = _cop_l;
        } else {
            cop = _cop_r;
        }

        if (_copStopActive && (abs(cop.point.x) > _copXThreshold || abs(cop.point.y) > _copYThreshold)) {
            _walkEngine.requestPause();
            if (abs(cop.point.x) > _copXThreshold) {
                ROS_WARN("cop x stop");
            } else {
                ROS_WARN("cop y stop");
            }
        }


    }

    void QuinticWalkingNode::robStateCb(const humanoid_league_msgs::RobotControlState msg) {
        _robotState = msg.state;
    }

    void QuinticWalkingNode::jointStateCb(const sensor_msgs::JointState msg) {
        std::vector<std::string> names_vec = msg.name;
        std::string *names = names_vec.data();

        _current_state->setJointPositions(*names, msg.position.data());
    }

    void QuinticWalkingNode::kickCb(const std_msgs::BoolConstPtr msg) {
        _walkEngine.requestKick(msg->data);
    }

    void QuinticWalkingNode::cop_l_cb(const geometry_msgs::PointStamped msg) {
        _cop_l = msg;
    }

    void QuinticWalkingNode::cop_r_cb(const geometry_msgs::PointStamped msg) {
        _cop_r = msg;
    }


    void
    QuinticWalkingNode::reconf_callback(bitbots_quintic_walk::bitbots_quintic_walk_paramsConfig &config,
                                        uint32_t level) {
        _params = config;

        _walkEngine.reconf_callback(_params);
        _bioIK_solver.set_bioIK_timeout(config.bioIKTime);

        _debugActive = config.debugActive;
        _engineFrequency = config.engineFreq;
        _odomPubFactor = config.odomPubFactor;

        _max_step[0] = config.maxStepX;
        _max_step[1] = config.maxStepY;
        _max_step[2] = config.maxStepZ;
        _max_step_xy = config.maxStepXY;

        _imuActive = config.imuActive;
        _imu_pitch_threshold = config.imuPitchThreshold;
        _imu_roll_threshold = config.imuRollThreshold;
        _imu_pitch_vel_threshold = config.imuPitchVelThreshold;
        _imu_roll_vel_threshold = config.imuRollVelThreshold;

        _phaseResetActive = config.phaseResetActive;
        _phaseResetPhase = config.phaseResetPhase;
        _groundMinPressure = config.groundMinPressure;
        _copStopActive = config.copStopActive;
        _copXThreshold = config.copXThreshold;
        _copYThreshold = config.copYThreshold;
        _pressureStopActive = config.pressureStopActive;
        _ioPressureThreshold = config.ioPressureThreshold;
        _fbPressureThreshold = config.fbPressureThreshold;
        _params.pauseDuration = config.pauseDuration;
    }


    void
    QuinticWalkingNode::publishControllerCommands(std::vector<std::string> joint_names, std::vector<double> positions) {
        // publishes the commands to the GroupedPositionController
        _command_msg.header.stamp = ros::Time::now();
        _command_msg.joint_names = joint_names;
        _command_msg.positions = positions;
        std::vector<double> ones(joint_names.size(), -1.0);
        std::vector<double> vels(joint_names.size(), -1.0);
        std::vector<double> accs(joint_names.size(), -1.0);
        std::vector<double> pwms(joint_names.size(), -1.0);
        _command_msg.velocities = vels;
        _command_msg.accelerations = accs;
        _command_msg.max_currents = pwms;

        _pubControllerCommand.publish(_command_msg);
    }

    void QuinticWalkingNode::publishOdometry() {
        // transformation from support leg to trunk
        Eigen::Isometry3d trunk_to_support;
        if (_walkEngine.getFootstep().isLeftSupport()) {
            trunk_to_support = _goal_state->getGlobalLinkTransform("l_sole");
        } else {
            trunk_to_support = _goal_state->getGlobalLinkTransform("r_sole");
        }
        Eigen::Isometry3d support_to_trunk = trunk_to_support.inverse();
        tf2::Transform tf_support_to_trunk;
        tf2::convert(support_to_trunk, tf_support_to_trunk);

        // odometry to trunk is transform to support foot * transform from support to trunk
        double x;
        double y;
        double yaw;
        if (_walkEngine.getFootstep().isLeftSupport()) {
            x = _walkEngine.getFootstep().getLeft()[0];
            y = _walkEngine.getFootstep().getLeft()[1] + _params.footDistance / 2;
            yaw = _walkEngine.getFootstep().getLeft()[2];
        } else {
            x = _walkEngine.getFootstep().getRight()[0];
            y = _walkEngine.getFootstep().getRight()[1] + _params.footDistance / 2;
            yaw = _walkEngine.getFootstep().getRight()[2];
        }

        tf2::Transform supportFootTf;
        supportFootTf.setOrigin(tf2::Vector3{x, y, 0.0});
        tf2::Quaternion supportFootQuat;
        supportFootQuat.setRPY(0, 0, yaw);
        supportFootTf.setRotation(supportFootQuat);
        tf2::Transform odom_to_trunk = supportFootTf * tf_support_to_trunk;
        tf2::Vector3 pos = odom_to_trunk.getOrigin();
        geometry_msgs::Quaternion quat_msg;

        tf2::convert(odom_to_trunk.getRotation().normalize(), quat_msg);

        ros::Time current_time = ros::Time::now();

        if (_publishOdomTF) {
            _odom_trans = geometry_msgs::TransformStamped();
            _odom_trans.header.stamp = current_time;
            _odom_trans.header.frame_id = "odom";
            _odom_trans.child_frame_id = "base_link";

            _odom_trans.transform.translation.x = pos[0];
            _odom_trans.transform.translation.y = pos[1];
            _odom_trans.transform.translation.z = pos[2];
            _odom_trans.transform.rotation = quat_msg;

            //send the transform
            _odom_broadcaster.sendTransform(_odom_trans);
        }

        // send the odometry also as message
        _odom_msg.header.stamp = current_time;
        _odom_msg.header.frame_id = "odom";
        _odom_msg.child_frame_id = "base_link";
        _odom_msg.pose.pose.position.x = pos[0];
        _odom_msg.pose.pose.position.y = pos[1];
        _odom_msg.pose.pose.position.z = pos[2];

        _odom_msg.pose.pose.orientation = quat_msg;
        geometry_msgs::Twist twist;

        twist.linear.x = _currentOrders[0] * _params.freq * 2;
        twist.linear.y = _currentOrders[1] * _params.freq * 2;
        twist.angular.z = _currentOrders[2] * _params.freq * 2;

        _odom_msg.twist.twist = twist;
        _pubOdometry.publish(_odom_msg);
    }

    void
    QuinticWalkingNode::publishDebug(tf2::Transform &trunk_to_support_foot_goal,
                                     tf2::Transform &trunk_to_flying_foot_goal) {
        /*
        This method publishes various debug / visualization information.
        */
        bitbots_quintic_walk::WalkingDebug msg;
        bool is_left_support = _walkEngine.isLeftSupport();
        msg.is_left_support = is_left_support;
        msg.is_double_support = _walkEngine.isDoubleSupport();
        msg.header.stamp = ros::Time::now();

        // define current support frame
        std::string current_support_frame;
        if (is_left_support) {
            current_support_frame = "l_sole";
        } else {
            current_support_frame = "r_sole";
        }

        // define colors based on current support state
        float r, g, b, a;
        if (_walkEngine.isDoubleSupport()) {
            r = 0;
            g = 0;
            b = 1;
            a = 1;
        } else if (_walkEngine.isLeftSupport()) {
            r = 1;
            g = 0;
            b = 0;
            a = 1;
        } else {
            r = 1;
            g = 1;
            b = 0;
            a = 1;
        }


        // times
        msg.phase_time = _walkEngine.getPhase();
        msg.traj_time = _walkEngine.getTrajsTime();

        msg.engine_state.data = _walkEngine.getState();

        // footsteps
        msg.footstep_last.x = _walkEngine.getFootstep().getLast()[0];
        msg.footstep_last.y = _walkEngine.getFootstep().getLast()[1];
        msg.footstep_last.z = _walkEngine.getFootstep().getLast()[2];

        msg.footstep_next.x = _walkEngine.getFootstep().getNext()[0];
        msg.footstep_next.y = _walkEngine.getFootstep().getNext()[1];
        msg.footstep_next.z = _walkEngine.getFootstep().getNext()[2];



        // engine output
        geometry_msgs::Pose pose_msg;
        tf2::convert(_footPos, pose_msg.position);
        tf2::Quaternion tf_quat;
        tf_quat.setRPY(_footAxis[0], _footAxis[1], _footAxis[2]);
        tf2::convert(tf_quat, pose_msg.orientation);
        msg.engine_fly_goal = pose_msg;
        publishMarker("engine_fly_goal", current_support_frame, pose_msg, 0, 0, 1, a);

        msg.engine_fly_axis.x = _footAxis[0];
        msg.engine_fly_axis.x = _footAxis[1];
        msg.engine_fly_axis.x = _footAxis[2];


        tf2::convert(_trunkPos, pose_msg.position);
        tf_quat.setRPY(_trunkAxis[0], _trunkAxis[1], _trunkAxis[2]);
        tf2::convert(tf_quat, pose_msg.orientation);
        msg.engine_trunk_goal = pose_msg;
        publishMarker("engine_trunk_goal", current_support_frame, pose_msg, r, g, b, a);

        if (_trunkPos[1] > 0) {
            _trunkPos[1] = _trunkPos[1] - _params.footDistance / 2;
        } else {
            _trunkPos[1] = _trunkPos[1] + _params.footDistance / 2;
        }
        tf2::convert(_trunkPos, pose_msg.position);
        msg.engine_trunk_goal_abs = pose_msg;

        msg.engine_trunk_axis.x = _trunkAxis[0];
        msg.engine_trunk_axis.y = _trunkAxis[1];
        msg.engine_trunk_axis.z = _trunkAxis[2];

        // resulting trunk pose
        geometry_msgs::Pose pose;
        geometry_msgs::Point point;
        point.x = 0;
        point.y = 0;
        point.z = 0;
        pose.position = point;
        publishMarker("trunk_result", "base_link", pose, r, g, b, a);

        // goals
        geometry_msgs::Pose pose_support_foot_goal;
        tf2::toMsg(trunk_to_support_foot_goal, pose_support_foot_goal);
        msg.support_foot_goal = pose_support_foot_goal;
        geometry_msgs::Pose pose_fly_foot_goal;
        tf2::toMsg(trunk_to_flying_foot_goal, pose_fly_foot_goal);
        msg.fly_foot_goal = pose_fly_foot_goal;
        if (is_left_support) {
            msg.left_foot_goal = pose_support_foot_goal;
            msg.right_foot_goal = pose_fly_foot_goal;
        } else {
            msg.left_foot_goal = pose_fly_foot_goal;
            msg.right_foot_goal = pose_support_foot_goal;
        }
        publishMarker("engine_left_goal", "base_link", msg.left_foot_goal, 0, 1, 0, 1);
        publishMarker("engine_right_goal", "base_link", msg.right_foot_goal, 1, 0, 0, 1);

        // IK results
        geometry_msgs::Pose pose_left_result;
        tf2::convert(_goal_state->getGlobalLinkTransform("l_sole"), pose_left_result);
        msg.left_foot_ik_result = pose_left_result;
        geometry_msgs::Pose pose_right_result;
        tf2::convert(_goal_state->getGlobalLinkTransform("r_sole"), pose_right_result);
        msg.right_foot_ik_result = pose_right_result;
        if (is_left_support) {
            msg.support_foot_ik_result = pose_left_result;
            msg.fly_foot_ik_result = pose_right_result;
        } else {
            msg.support_foot_ik_result = pose_right_result;
            msg.fly_foot_ik_result = pose_left_result;
        }
        publishMarker("ik_left", "base_link", pose_left_result, 0, 1, 0, 1);
        publishMarker("ik_right", "base_link", pose_right_result, 1, 0, 0, 1);

        // IK offsets
        tf2::Vector3 support_off;
        tf2::Vector3 fly_off;
        tf2::Vector3 tf_vec_left;
        tf2::Vector3 tf_vec_right;
        Eigen::Vector3d l_transform = _goal_state->getGlobalLinkTransform("l_sole").translation();
        Eigen::Vector3d r_transform = _goal_state->getGlobalLinkTransform("r_sole").translation();
        tf2::convert(l_transform, tf_vec_left);
        tf2::convert(r_transform, tf_vec_right);
        geometry_msgs::Vector3 vect_msg;
        if (is_left_support) {
            support_off = trunk_to_support_foot_goal.getOrigin() - tf_vec_left;
            fly_off = trunk_to_flying_foot_goal.getOrigin() - tf_vec_right;
            tf2::convert(support_off, vect_msg);
            msg.left_foot_ik_offset = vect_msg;
            tf2::convert(fly_off, vect_msg);
            msg.right_foot_ik_offset = vect_msg;
        } else {
            support_off = trunk_to_support_foot_goal.getOrigin() - tf_vec_right;
            fly_off = trunk_to_flying_foot_goal.getOrigin() - tf_vec_left;
            tf2::convert(fly_off, vect_msg);
            msg.left_foot_ik_offset = vect_msg;
            tf2::convert(support_off, vect_msg);
            msg.right_foot_ik_offset = vect_msg;
        }
        tf2::convert(support_off, vect_msg);
        msg.support_foot_ik_offset = vect_msg;
        tf2::convert(fly_off, vect_msg);
        msg.fly_foot_ik_offset = vect_msg;

        // actual positions
        geometry_msgs::Pose pose_left_actual;
        tf2::convert(_current_state->getGlobalLinkTransform("l_sole"), pose_left_actual);
        msg.left_foot_position = pose_left_actual;
        geometry_msgs::Pose pose_right_actual;
        tf2::convert(_current_state->getGlobalLinkTransform("r_sole"), pose_right_actual);
        msg.right_foot_position = pose_right_actual;
        if (is_left_support) {
            msg.support_foot_position = pose_left_actual;
            msg.fly_foot_position = pose_right_actual;
        } else {
            msg.support_foot_position = pose_right_actual;
            msg.fly_foot_position = pose_left_actual;
        }

        // actual offsets
        l_transform = _current_state->getGlobalLinkTransform("l_sole").translation();
        r_transform = _current_state->getGlobalLinkTransform("r_sole").translation();
        tf2::convert(l_transform, tf_vec_left);
        tf2::convert(r_transform, tf_vec_right);
        if (is_left_support) {
            support_off = trunk_to_support_foot_goal.getOrigin() - tf_vec_left;
            fly_off = trunk_to_flying_foot_goal.getOrigin() - tf_vec_right;
            tf2::convert(support_off, vect_msg);
            msg.left_foot_actual_offset = vect_msg;
            tf2::convert(fly_off, vect_msg);
            msg.right_foot_actual_offset = vect_msg;
        } else {
            support_off = trunk_to_support_foot_goal.getOrigin() - tf_vec_right;
            fly_off = trunk_to_flying_foot_goal.getOrigin() - tf_vec_left;
            tf2::convert(fly_off, vect_msg);
            msg.left_foot_actual_offset = vect_msg;
            tf2::convert(support_off, vect_msg);
            msg.right_foot_actual_offset = vect_msg;
        }
        tf2::convert(support_off, vect_msg);
        msg.support_foot_actual_offset = vect_msg;
        tf2::convert(fly_off, vect_msg);
        msg.fly_foot_actual_offset = vect_msg;

        _pubDebug.publish(msg);
    }

    void QuinticWalkingNode::publishMarker(std::string name_space,
                                           std::string frame,
                                           geometry_msgs::Pose pose, float r, float g, float b, float a) {
        visualization_msgs::Marker marker_msg;
        marker_msg.header.stamp = ros::Time::now();
        marker_msg.header.frame_id = frame;

        marker_msg.type = marker_msg.ARROW;
        marker_msg.ns = name_space;
        marker_msg.action = marker_msg.ADD;
        marker_msg.pose = pose;


        std_msgs::ColorRGBA color;
        color.r = r;
        color.g = g;
        color.b = b;
        color.a = a;
        marker_msg.color = color;

        geometry_msgs::Vector3 scale;
        scale.x = 0.01;
        scale.y = 0.003;
        scale.z = 0.003;
        marker_msg.scale = scale;

        marker_msg.id = _marker_id;
        _marker_id++;

        _pubDebugMarker.publish(marker_msg);
    }

    void QuinticWalkingNode::publishMarkers() {
        //publish markers
        visualization_msgs::Marker marker_msg;
        marker_msg.header.stamp = ros::Time::now();
        if (_walkEngine.getFootstep().isLeftSupport()) {
            marker_msg.header.frame_id = "l_sole";
        } else {
            marker_msg.header.frame_id = "r_sole";
        }
        marker_msg.type = marker_msg.CUBE;
        marker_msg.action = 0;
        marker_msg.lifetime = ros::Duration(0.0);
        geometry_msgs::Vector3 scale;
        scale.x = 0.20;
        scale.y = 0.10;
        scale.z = 0.01;
        marker_msg.scale = scale;
        //last step
        marker_msg.ns = "last_step";
        marker_msg.id = 1;
        std_msgs::ColorRGBA color;
        color.r = 0;
        color.g = 0;
        color.b = 0;
        color.a = 1;
        marker_msg.color = color;
        geometry_msgs::Pose pose;
        Eigen::Vector3d step_pos = _walkEngine.getFootstep().getLast();
        geometry_msgs::Point point;
        point.x = step_pos[0];
        point.y = step_pos[1];
        point.z = 0;
        pose.position = point;
        tf2::Quaternion q;
        q.setRPY(0.0, 0.0, step_pos[2]);
        tf2::convert(q, pose.orientation);
        marker_msg.pose = pose;
        _pubDebugMarker.publish(marker_msg);

        //last step center
        marker_msg.ns = "step_center";
        marker_msg.id = _marker_id;
        scale.x = 0.01;
        scale.y = 0.01;
        scale.z = 0.01;
        marker_msg.scale = scale;
        _pubDebugMarker.publish(marker_msg);

        // next step
        marker_msg.id = _marker_id;
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
        step_pos = _walkEngine.getFootstep().getNext();
        point.x = step_pos[0];
        point.y = step_pos[1];
        pose.position = point;
        q.setRPY(0.0, 0.0, step_pos[2]);
        tf2::convert(q, pose.orientation);
        marker_msg.pose = pose;
        _pubDebugMarker.publish(marker_msg);

        _marker_id++;
    }

    void QuinticWalkingNode::initializeEngine() {
        _walkEngine.reset();
    }

}

int main(int argc, char **argv) {
    ros::init(argc, argv, "quintic_walking");
    // init node
    bitbots_quintic_walk::QuinticWalkingNode node;

    // run the node
    node.initializeEngine();
    node.run();
}
