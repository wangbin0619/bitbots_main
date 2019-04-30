#!/usr/bin/env python3

import argparse

import rospy
from sensor_msgs.msg import JointState
from bitbots_msgs.msg import JointCommand
from humanoid_league_msgs.msg import Animation

class MotorVizHelper:
    def __init__(self):
        # get rid of addional ROS args when used in launch file
        args0 = rospy.myargv()

        parser = argparse.ArgumentParser()
        parser.add_argument("--walking", "-w", help="Directly get walking motor goals", action="store_true")
        parser.add_argument("--animation", "-a", help="Directly get animation motor goals", action="store_true")
        args = parser.parse_args(args0[1:])

        rospy.init_node("motor_viz_helper", anonymous=False)
        self.joint_state_msg = JointState()
        self.joint_publisher = rospy.Publisher('joint_states', JointState, queue_size=1, tcp_nodelay=True)
        if args.walking:
            rospy.Subscriber("walking_motor_goals", JointCommand, self.joint_command_cb, queue_size=1, tcp_nodelay=True)
        elif args.animation:
            rospy.Subscriber("animation", Animation, self.animation_cb, queue_size=1, tcp_nodelay=True)
        else:
            rospy.Subscriber("/DynamixelController/command", JointCommand, self.joint_command_cb, queue_size=1, tcp_nodelay=True)

        rate = rospy.Rate(100)
        while not rospy.is_shutdown():
            self.joint_state_msg.header.stamp = rospy.Time.now()
            self.joint_publisher.publish(self.joint_state_msg)
            rate.sleep()

    def joint_command_cb(self, msg:JointCommand):
        self.joint_state_msg.header = msg.header
        self.joint_state_msg.name = msg.joint_names
        self.joint_state_msg.position = msg.positions

    def animation_cb(self, msg:Animation):
        self.joint_state_msg.header = msg.header
        self.joint_state_msg.name = msg.position.joint_names
        self.joint_state_msg.position = msg.position.points[0].positions

helper = MotorVizHelper()

