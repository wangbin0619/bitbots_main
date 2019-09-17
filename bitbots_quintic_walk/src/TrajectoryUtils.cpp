/*
This code is largely based on the original code by Quentin "Leph" Rouxel and Team Rhoban.
The original files can be found at:
https://github.com/Rhoban/model/
*/
#include "bitbots_quintic_walk/TrajectoryUtils.h"
#include "bitbots_splines/AxisAngle.h"

namespace bitbots_quintic_walk {

Trajectories trajectoriesInit() {
  Trajectories traj;
  traj.add("is_double_support");
  traj.add("is_left_support_foot");
  traj.add("trunk_pos_x");
  traj.add("trunk_pos_y");
  traj.add("trunk_pos_z");
  traj.add("trunk_axis_x");
  traj.add("trunk_axis_y");
  traj.add("trunk_axis_z");
  traj.add("foot_pos_x");
  traj.add("foot_pos_y");
  traj.add("foot_pos_z");
  traj.add("foot_axis_x");
  traj.add("foot_axis_y");
  traj.add("foot_axis_z");

  return traj;
}

void trajectoriesTrunkFootPos(
    double t, const Trajectories &traj,
    Eigen::Vector3d &trunk_pos,
    Eigen::Vector3d &trunk_axis,
    Eigen::Vector3d &foot_pos,
    Eigen::Vector3d &foot_axis) {
  //Compute Cartesian positions
  trunk_pos = Eigen::Vector3d(
      traj.get("trunk_pos_x").pos(t),
      traj.get("trunk_pos_y").pos(t),
      traj.get("trunk_pos_z").pos(t));
  trunk_axis = Eigen::Vector3d(
      traj.get("trunk_axis_x").pos(t),
      traj.get("trunk_axis_y").pos(t),
      traj.get("trunk_axis_z").pos(t));
  foot_pos = Eigen::Vector3d(
      traj.get("foot_pos_x").pos(t),
      traj.get("foot_pos_y").pos(t),
      traj.get("foot_pos_z").pos(t));
  foot_axis = Eigen::Vector3d(
      traj.get("foot_axis_x").pos(t),
      traj.get("foot_axis_y").pos(t),
      traj.get("foot_axis_z").pos(t));
}
void trajectoriesTrunkFootVel(
    double t, const Trajectories &traj,
    Eigen::Vector3d &trunk_pos_vel,
    Eigen::Vector3d &trunk_axis_vel,
    Eigen::Vector3d &foot_pos_vel,
    Eigen::Vector3d &foot_axis_vel) {
  //Compute Cartesian velocities
  trunk_pos_vel = Eigen::Vector3d(
      traj.get("trunk_pos_x").vel(t),
      traj.get("trunk_pos_y").vel(t),
      traj.get("trunk_pos_z").vel(t));
  trunk_axis_vel = Eigen::Vector3d(
      traj.get("trunk_axis_x").vel(t),
      traj.get("trunk_axis_y").vel(t),
      traj.get("trunk_axis_z").vel(t));
  foot_pos_vel = Eigen::Vector3d(
      traj.get("foot_pos_x").vel(t),
      traj.get("foot_pos_y").vel(t),
      traj.get("foot_pos_z").vel(t));
  foot_axis_vel = Eigen::Vector3d(
      traj.get("foot_axis_x").vel(t),
      traj.get("foot_axis_y").vel(t),
      traj.get("foot_axis_z").vel(t));
}
void trajectoriesTrunkFootAcc(
    double t, const Trajectories &traj,
    Eigen::Vector3d &trunk_pos_acc,
    Eigen::Vector3d &trunk_axis_acc,
    Eigen::Vector3d &foot_pos_acc,
    Eigen::Vector3d &foot_axis_acc) {
  //Compute Cartesian accelerations
  trunk_pos_acc = Eigen::Vector3d(
      traj.get("trunk_pos_x").acc(t),
      traj.get("trunk_pos_y").acc(t),
      traj.get("trunk_pos_z").acc(t));
  trunk_axis_acc = Eigen::Vector3d(
      traj.get("trunk_axis_x").acc(t),
      traj.get("trunk_axis_y").acc(t),
      traj.get("trunk_axis_z").acc(t));
  foot_pos_acc = Eigen::Vector3d(
      traj.get("foot_pos_x").acc(t),
      traj.get("foot_pos_y").acc(t),
      traj.get("foot_pos_z").acc(t));
  foot_axis_acc = Eigen::Vector3d(
      traj.get("foot_axis_x").acc(t),
      traj.get("foot_axis_y").acc(t),
      traj.get("foot_axis_z").acc(t));
}
void trajectoriesSupportFootState(
    double t, const Trajectories &traj,
    bool &isDoubleSupport,
    bool &isLeftsupportFoot) {
  //Compute support foot state
  isDoubleSupport = (
      traj.get("is_double_support").pos(t) >= 0.5 ?
      true : false);
  isLeftsupportFoot = (
      traj.get("is_left_support_foot").pos(t) >= 0.5 ?
      true : false);
}

double DefaultCheckState(
    const Eigen::VectorXd &params,
    double t,
    const Eigen::Vector3d &trunkPos,
    const Eigen::Vector3d &trunkAxis,
    const Eigen::Vector3d &footPos,
    const Eigen::Vector3d &footAxis) {
  (void) params;
  (void) t;
  double cost = 0.0;
  if (trunkPos.z() < 0.0) {
    cost += 1000.0 - 1000.0*trunkPos.z();
  }
  if (trunkAxis.norm() >= M_PI/2.0) {
    cost += 1000.0 + 1000.0*(trunkAxis.norm() - M_PI/2.0);
  }
  if (fabs(footPos.y()) < 2.0*0.037) {
    cost += 1000.0 + 1000.0*(2.0*0.037 - fabs(footPos.y()));
  }
  if (footPos.z() < -1e6) {
    cost += 1000.0 - 1000.0*footPos.z();
  }
  if (trunkPos.z() - footPos.z() < 0.20) {
    cost += 1000.0 - 1000.0*(trunkPos.z() - footPos.z() - 0.20);
  }
  if (footAxis.norm() >= M_PI/2.0) {
    cost += 1000.0 + 1000.0*(footAxis.norm() - M_PI/2.0);
  }

  return cost;
}

}

