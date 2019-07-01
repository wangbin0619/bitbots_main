#include "bitbots_dynup/Stabilizer.h"
#include "bitbots_dynup/DynamicBalancingGoal.h"
#include "bitbots_dynup/ReferencePoseGoal.h"
#include "bitbots_dynup/ReferenceOrientationGoal.h"

Stabilizer::Stabilizer() {
    /* load MoveIt! model */
    robot_model_loader::RobotModelLoader robot_model_loader("/robot_description", false);
    robot_model_loader.loadKinematicsSolvers(
            kinematics_plugin_loader::KinematicsPluginLoaderPtr(
                    new kinematics_plugin_loader::KinematicsPluginLoader()));

    /* Extract joint groups from loaded model */
    m_kinematic_model = robot_model_loader.getModel();
    m_all_joints_group = m_kinematic_model->getJointModelGroup("All");

    /* Reset kinematic goal to default */
    m_goal_state.reset(new robot_state::RobotState(m_kinematic_model));
    m_goal_state->setToDefaultValues();

    /* We have to set some good initial position in the goal state,
     * since we are using a gradient based method. Otherwise, the
     * first step will be not correct */
     //TODO
    std::vector<std::string> names_vec = {"LHipPitch", "LKnee", "LAnklePitch", "RHipPitch", "RKnee", "RAnklePitch"};
    std::vector<double> pos_vec = {0.7, -1.0, -0.4, -0.7, 1.0, 0.4};
    for (int i = 0; i < names_vec.size(); i++) {
        m_goal_state->setJointPositions(names_vec[i], &pos_vec[i]);
    }
}

std::optional<JointGoals> Stabilizer::stabilize(geometry_msgs::Point support_point, geometry_msgs::PoseStamped l_foot_goal_pose, geometry_msgs::PoseStamped r_foot_goal_pose) {
    /* ik options is basicaly the command which we send to bio_ik and which describes what we want to do */
    bio_ik::BioIKKinematicsQueryOptions ik_options;
    ik_options.replace = true;
    ik_options.return_approximate_solution = true;
    double bio_ik_timeout = 0.01;


    /* construct the bio_ik Pose object which tells bio_ik what we want to achieve */
    auto *bio_ik_l_foot_goal = new PoseGoal();
    bio_ik_l_foot_goal->setPosition(l_foot_goal_pose.getOrigin());
    bio_ik_l_foot_goal->setOrientation(l_foot_goal_pose.getRotation());
    bio_ik_l_foot_goal->setLinkName("l_sole");
    bio_ik_l_foot_goal->setWeight(m_l_foot_weight);

    auto *bio_ik_r_foot_goal = new PoseGoal();
    bio_ik_r_foot_goal->setPosition(l_foot_goal_pose.getOrigin());
    bio_ik_r_foot_goal->setOrientation(l_foot_goal_pose.getRotation());
    bio_ik_r_foot_goal->setLinkName("l_sole");
    bio_ik_r_foot_goal->setWeight(m_r_foot_weight);

    auto *bio_ik_l_hand_goal = new PoseGoal();
    bio_ik_l_hand_goal->setPosition(l_foot_goal_pose.getOrigin());
    bio_ik_l_hand_goal->setOrientation(l_foot_goal_pose.getRotation());
    bio_ik_l_hand_goal->setLinkName("l_sole");
    bio_ik_l_hand_goal->setWeight(m_l_hand_goal);

    auto *bio_ik_r_hand_goal = new PoseGoal();
    bio_ik_r_hand_goal->setPosition(l_foot_goal_pose.getOrigin());
    bio_ik_r_hand_goal->setOrientation(l_foot_goal_pose.getRotation());
    bio_ik_r_hand_goal->setLinkName("l_sole");
    bio_ik_r_hand_goal->setWeight(m_r_hand_goal);

    DynamicBalancingContext bio_ik_balancing_context(m_kinematic_model);
    auto *bio_ik_balance_goal = new DynamicBalancingGoal(&bio_ik_balancing_context, stabilizing_target, m_stabilizing_weight);
    bio_ik_balance_goal->setReferenceLink("base_link");


    ik_options.goals.emplace_back(bio_ik_l_foot_goal);
    ik_options.goals.emplace_back(bio_ik_r_foot_goal);
    ik_options.goals.emplace_back(bio_ik_l_hand_goal);
    ik_options.goals.emplace_back(bio_ik_r_hand_goal);

    if (m_use_stabilizing) {
        ik_options.goals.emplace_back(bio_ik_balance_goal);
    }
    if (m_use_minimal_displacement) {
        ik_options.goals.emplace_back(new bio_ik::MinimalDisplacementGoal());
    }

    bool success = m_goal_state->setFromIK(m_legs_joints_group,
                                           EigenSTL::vector_Isometry3d(),
                                           std::vector<std::string>(),
                                           bio_ik_timeout,
                                           moveit::core::GroupStateValidityCallbackFn(),
                                           ik_options);

    if (success) {
        /* retrieve joint names and associated positions from  */
        std::vector<std::string> joint_names = m_legs_joints_group->getActiveJointModelNames();
        std::vector<double> joint_goals;
        m_goal_state->copyJointGroupPositions(m_legs_joints_group, joint_goals);

        /* construct result object */
        JointGoals result;
        result.first = joint_names;
        result.second = joint_goals;
        return result;
    } else {
        return std::nullopt;
    }
}

void Stabilizer::use_stabilizing(bool use) {
    m_use_stabilizing = use;
}

void Stabilizer::use_minimal_displacement(bool use) {
    m_use_minimal_displacement = use;
}

void Stabilizer::set_stabilizing_weight(double weight) {
    m_stabilizing_weight = weight;
}

void Stabilizer::set_flying_weight(double weight) {
    m_flying_weight = weight;
}

void Stabilizer::set_trunk_orientation_weight(double weight) {
    m_trunk_orientation_weight = weight;
}
