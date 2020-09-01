#include <bitbots_dynup/dynup_ik.h>
namespace bitbots_dynup {
void DynupIK::init(moveit::core::RobotModelPtr kinematic_model) {
  /* Extract joint groups from kinematics model */
  all_joints_group_.reset(kinematic_model->getJointModelGroup("All"));


  /* Reset kinematic goal to default */
  goal_state_.reset(new robot_state::RobotState(kinematic_model));
  goal_state_->setToDefaultValues();
}

void DynupIK::reset() {
  /* We have to set some good initial position in the goal state,
   * since we are using a gradient based method. Otherwise, the
   * first step will be not correct */
  //TODO: add hands or replace with actual values
  std::vector<std::string> names_vec = {"HeadPan", "HeadTilt", "LElbow", "LShoulderPitch", "LShoulderRoll", "RElbow", "RShoulderPitch", "RShoulderRoll", "LHipYaw", "LHipRoll", "LHipPitch", "LKnee", "LAnklePitch", "LAnkleRoll", "RHipYaw", "RHipRoll", "RHipPitch", "RKnee", "RAnklePitch", "RAnkleRoll"};
  std::vector<double> pos_vec = {0, 0, 0.8, 0, 0, -0.8, 0, 0, 0, 0.1, 0.5, -1.0, -0.4, 0.1, 0, -0.1, -0.5, -1.0, 0.4, -0.1};
  for (int i = 0; i < names_vec.size(); ++i) {
    goal_state_->setJointPositions(names_vec[i], &pos_vec[i]);
  }

}

bitbots_splines::JointGoals DynupIK::calculate(std::unique_ptr<bio_ik::BioIKKinematicsQueryOptions> ik_goals) {
  //TODO: add arm IK!
  double bio_ik_timeout = 0.01;
  bool success = goal_state_->setFromIK(all_joints_group_.get(),
                                        EigenSTL::vector_Isometry3d(),
                                        std::vector<std::string>(),
                                        bio_ik_timeout,
                                        moveit::core::GroupStateValidityCallbackFn(),
                                        *ik_goals);

  if (success) {
    /* retrieve joint names and associated positions from  */
    std::vector<std::string> joint_names = all_joints_group_->getActiveJointModelNames();
    std::vector<double> joint_goals;
    goal_state_->copyJointGroupPositions(all_joints_group_.get(), joint_goals);

    /* construct result object */
    bitbots_splines::JointGoals result;
    result.first = joint_names;
    result.second = joint_goals;
    return result;
  } else {
    return {};
  }
}

}
