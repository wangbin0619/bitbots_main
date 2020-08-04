#include <ros/callback_queue.h>
#include <controller_manager/controller_manager.h>
#include <bitbots_ros_control/wolfgang_hardware_interface.h>

int main(int argc, char *argv[]) {
  ros::init(argc, argv, "ros_control");
  ros::NodeHandle pnh("~");

  // create hardware interfaces
  bitbots_ros_control::WolfgangHardwareInterface hw(pnh);

  if (!hw.init(pnh)) {
    ROS_ERROR_STREAM("Failed to initialize hardware interface.");
    return 1;
  }

  // Create separate queue, because otherwise controller manager will freeze
  ros::NodeHandle nh;
  ros::CallbackQueue queue;
  nh.setCallbackQueue(&queue);
  ros::AsyncSpinner spinner(1, &queue);
  spinner.start();
  controller_manager::ControllerManager cm(&hw, nh);

  // Start control loop
  ros::Time current_time = ros::Time::now();
  ros::Duration period = ros::Time::now() - current_time;
  bool first_update = true;
  ros::Rate rate(pnh.param("control_loop_hz", 200));

  while (ros::ok()) {
    hw.read(current_time, period);
    period = ros::Time::now() - current_time;
    current_time = ros::Time::now();

    // period only makes sense after the first update
    // therefore, the controller manager is only updated starting with the second iteration
    if (first_update) {
      first_update = false;
    } else {
      cm.update(current_time, period);
    }
    hw.write(current_time, period);

    rate.sleep();
    ros::spinOnce();
  }
  return 0;
}
