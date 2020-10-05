#include <bitbots_ros_control/dynamixel_controller.hpp>
#include <pluginlib/class_list_macros.hpp>
#include <bitbots_msgs/JointCommand.h>

namespace dynamixel_controller {
bool DynamixelController::init(hardware_interface::PosVelAccCurJointInterface *hw, ros::NodeHandle &n) {
  // Get list of controlled joints from paramserver
  std::string param_name = "joints";
  if (!n.getParam(param_name, joint_names)) {
    ROS_ERROR_STREAM("Failed to getParam '" << param_name << "' (namespace: " << n.getNamespace() << ").");
    return false;
  }
  n_joints = joint_names.size();

  if (n_joints == 0) {
    ROS_ERROR_STREAM("List of joint names is empty.");
    return false;
  }
  // get handles for joints
  for (unsigned int i = 0; i < n_joints; i++) {
    try {
      joints.push_back(hw->getHandle(joint_names[i]));
      joint_map_[joint_names[i]] = i;
    }
    catch (const hardware_interface::HardwareInterfaceException &e) {
      ROS_ERROR_STREAM("Exception thrown: " << e.what());
      return false;
    }
  }

  sub_command_ = n.subscribe("command", 1, &DynamixelController::commandCb, this, ros::TransportHints().tcpNoDelay());
  return true;
}

void DynamixelController::starting(const ros::Time &time) {}
void DynamixelController::update(const ros::Time & /*time*/, const ros::Duration & /*period*/) {
  std::vector<JointCommandData> &buf_data = *commands_buffer.readFromRT();

  // set command for all registered joints
  for (unsigned int i = 0; i < buf_data.size(); i++) {
    joints[buf_data[i].id].setCommand(buf_data[i].pos, buf_data[i].vel, buf_data[i].acc, buf_data[i].cur);
  }
}
}
PLUGINLIB_EXPORT_CLASS(dynamixel_controller::DynamixelController, controller_interface::ControllerBase
)
