#include <bitbots_ros_control/button_hardware_interface.h>

namespace bitbots_ros_control
{
ButtonHardwareInterface::ButtonHardwareInterface(){}

ButtonHardwareInterface::ButtonHardwareInterface(std::shared_ptr<DynamixelDriver>& driver){
  driver_ = driver;
}

bool ButtonHardwareInterface::init(ros::NodeHandle& nh){
  nh_ = nh;
  button_pub_ = nh.advertise<bitbots_buttons::Buttons>("/buttons", 1);
  return true;
}

bool ButtonHardwareInterface::read(){
  /**
   * Reads the buttons
   */
  counter_ = (counter_ + 1) % 100;
  if(counter_ != 0)
    return true;
  uint8_t *data = (uint8_t *) malloc(sizeof(uint8_t));
  if(driver_->readMultipleRegisters(241, 76, 3, data)){;
    bitbots_buttons::Buttons msg;
    msg.button1 = data[0];
    msg.button2 = data[1];
    msg.button3 = data[2];
    button_pub_.publish(msg);
    return true;
  }
  ROS_ERROR_THROTTLE(1.0, "Couldn't read Buttons");
  return false;
}

// we dont write anything to the buttons
void ButtonHardwareInterface::write(){}

}
