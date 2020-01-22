#include "bitbots_dynup/dynup_stabilizer.h"

#include <memory>
#include <utility>
#include <bitbots_splines/dynamic_balancing_goal.h>
#include <bitbots_splines/reference_goals.h>

namespace bitbots_dynup {

void Stabilizer::init(moveit::core::RobotModelPtr kinematic_model) {
  kinematic_model_ = std::move(kinematic_model);

  legs_joints_group_ = kinematic_model_->getJointModelGroup("Legs");

  /* Reset kinematic goal to default */
  goal_state_.reset(new robot_state::RobotState(kinematic_model_));
  goal_state_->setToDefaultValues();
}

void Stabilizer::reset() {
  /* We have to set some good initial position in the goal state,
   * since we are using a gradient based method. Otherwise, the
   * first step will be not correct */
  std::vector<std::string> names_vec =
      {"HeadPan", "HeadTilt", "LAnklePitch", "LAnkleRoll", "LElbow", "LHipPitch", "LHipRoll", "LHipYaw", "LKnee",
       "LShoulderPitch", "LShoulderRoll", "RAnklePitch", "RAnkleRoll", "RElbow", "RHipPitch", "RHipRoll", "RHipYaw",
       "RKnee", "RShoulderPitch", "RShoulderRoll"};
  std::vector<double> pos_vec = {0, 0, -25, 4, 45, 27, 4, -1, -58, 0, 25, -4, 45, -37, -4, 1, 0, 58, 0, 0};
  for (double &pos: pos_vec) {
    pos = pos / 180.0 * M_PI;
  }
  for (int i = 0; i < names_vec.size(); i++) {
    goal_state_->setJointPositions(names_vec[i], &pos_vec[i]);
  }
}

std::unique_ptr<bio_ik::BioIKKinematicsQueryOptions> Stabilizer::stabilize(const DynupResponse &response) {
  /* ik options is basicaly the command which we send to bio_ik and which describes what we want to do */
  auto ik_options = std::make_unique<bio_ik::BioIKKinematicsQueryOptions>();
  ik_options->replace = true;
  ik_options->return_approximate_solution = true;

  tf2::Stamped<tf2::Transform> tf_l_foot, tf_r_foot, tf_r_hand, tf_l_hand;
  tf2::convert(response.l_foot_goal_pose, tf_l_foot);
  tf2::convert(response.r_foot_goal_pose, tf_r_foot);
  tf2::convert(response.r_hand_goal_pose, tf_r_hand);
  tf2::convert(response.l_hand_goal_pose, tf_l_hand);

  /* construct the bio_ik Pose object which tells bio_ik what we want to achieve */
  auto *bio_ik_l_foot_goal = new ReferencePoseGoal();
  bio_ik_l_foot_goal->setPosition(tf_l_foot.getOrigin());
  bio_ik_l_foot_goal->setOrientation(tf_l_foot.getRotation());
  bio_ik_l_foot_goal->setLinkName("l_sole");
  bio_ik_l_foot_goal->setWeight(1.0);
  bio_ik_l_foot_goal->setReferenceLinkName("r_sole");

  auto *bio_ik_r_foot_goal = new ReferencePoseGoal();
  bio_ik_r_foot_goal->setPosition(tf_r_foot.getOrigin());
  bio_ik_r_foot_goal->setOrientation(tf_r_foot.getRotation());
  bio_ik_r_foot_goal->setLinkName("r_sole");
  bio_ik_r_foot_goal->setWeight(1.0);
  bio_ik_r_foot_goal->setReferenceLinkName("torso");

  auto *bio_ik_l_hand_goal = new ReferencePoseGoal();
  bio_ik_l_hand_goal->setPosition(tf_l_hand.getOrigin());
  bio_ik_l_hand_goal->setOrientation(tf_l_hand.getRotation());
  bio_ik_l_hand_goal->setLinkName("l_wrist");
  bio_ik_l_hand_goal->setWeight(1.0);
  bio_ik_l_hand_goal->setReferenceLinkName("torso");

  auto *bio_ik_r_hand_goal = new ReferencePoseGoal();
  bio_ik_r_hand_goal->setPosition(tf_r_hand.getOrigin());
  bio_ik_r_hand_goal->setOrientation(tf_r_hand.getRotation());
  bio_ik_r_hand_goal->setLinkName("r_wrist");
  bio_ik_r_hand_goal->setWeight(1.0);
  bio_ik_r_hand_goal->setReferenceLinkName("torso");

  tf2::Vector3 stabilizing_target = {response.support_point.x, response.support_point.y, response.support_point.z};
  auto *bio_ik_balancing_context = new DynamicBalancingContext(kinematic_model_);
  auto *bio_ik_balance_goal =
      new DynamicBalancingGoal(bio_ik_balancing_context, stabilizing_target, stabilizing_weight_);
  bio_ik_balance_goal->setReferenceLink("base_link");

  ik_options->goals.emplace_back(bio_ik_l_foot_goal);
  ik_options->goals.emplace_back(bio_ik_r_foot_goal);
  ik_options->goals.emplace_back(bio_ik_r_hand_goal);
  ik_options->goals.emplace_back(bio_ik_l_hand_goal);

  if (use_stabilizing_) {
    ik_options->goals.emplace_back(bio_ik_balance_goal);
  }
  if (use_minimal_displacement_) {
    ik_options->goals.emplace_back(new bio_ik::MinimalDisplacementGoal());
  }
  return std::move(ik_options);
}

void Stabilizer::useStabilizing(bool use) {
  use_stabilizing_ = use;
}

void Stabilizer::useMinimalDisplacement(bool use) {
  use_minimal_displacement_ = use;
}

void Stabilizer::setStabilizingWeight(double weight) {
  stabilizing_weight_ = weight;
}

}
