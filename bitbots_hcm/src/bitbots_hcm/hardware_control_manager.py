#!/usr/bin/env python
import argparse
import time
from math import asin

import math
from threading import Lock

import actionlib
import numpy
import rospy

from humanoid_league_msgs.msg import Animation as AnimationMsg, PlayAnimationAction

from humanoid_league_speaker.speaker import speak
from std_msgs.msg import Bool, String
from bitbots_ros_control.msg import JointCommand, JointTorque

from bitbots_cm730.srv import SwitchMotorPower

from bitbots_hcm.hcm_state_machine import HcmStateMachine, STATE_CONTROLABLE, AnimationRunning, STATE_WALKING, STATE_ANIMATION_RUNNING
from dynamic_reconfigure.server import Server
from humanoid_league_msgs.msg import RobotControlState
from humanoid_league_msgs.msg import Speak
from sensor_msgs.msg import Imu
from sensor_msgs.msg import JointState
from trajectory_msgs.msg import JointTrajectory, JointTrajectoryPoint

from bitbots_hcm.values import BLACKBOARD
from bitbots_hcm.cfg import hcm_paramsConfig

from bitbots_stackmachine.stack_machine import StackMachine
from bitbots_connector.connector import AbstractConnector


class Motion:
    def __init__(self, dieflag, standupflag, softoff_flag, softstart, start_test):

        # --- Class Variables ---
        # Setup
        self.first_run = True

        # IMU
        self.accel = numpy.array([0, 0, 0])
        self.gyro = numpy.array([0, 0, 0])
        self.smooth_accel = numpy.array([0, 0, 0])
        self.smooth_gyro = numpy.array([0, 0, 0])
        self.not_much_smoothed_gyro = numpy.array([0, 0, 0])

        # Motor Positions
        self.walking_motor_goal = None
        self.walking_goal_lock = Lock()
        self.last_walking_update = 0
        self.head_goal_lock = Lock()

        # Animation
        self.animation_running = False  # animation request from animation server
        self.animation_request_time = 0  # time we got the animation request

        #times
        self.anim_sum=0
        self.anim_count=0
        self.walk_sum = 0
        self.walk_count=0
        self.imu_sum =0
        self.imu_count = 0
        self.arrt = []
        self.arrn = []
        self.f = open("hcm_lat_imu", 'w')
        self.f2 = open("hcm_lat_walk", 'w')
        self.aw1 = []
        self.aw2 = []

        # stack machine
        self.connector = 
        self.stack_machine = StackMachine(self.connector, "debug_hcm_stack_machine")

        # --- Initialize Node ---
        log_level = rospy.DEBUG if rospy.get_param("debug_active", False) else rospy.INFO
        rospy.init_node('bitbots_hcm', log_level=log_level, anonymous=False)
        rospy.sleep(0.1)  # Otherwise messages will get lost, bc the init is not finished
        rospy.loginfo("Starting hcm")

        self.startup_time = rospy.get_time()
        self.last_gyro_update_time = rospy.get_time()

        self.joint_goal_publisher = rospy.Publisher('motor_goals', JointTrajectory, queue_size=1)
        self.joint_goal_command_publisher = rospy.Publisher('DynamixelController/command', JointCommand, queue_size=1)
        self.hcm_state_publisher = rospy.Publisher('robot_state', RobotControlState, queue_size=1, latch=True)
        self.speak_publisher = rospy.Publisher('speak', Speak, queue_size=1)
        BLACKBOARD.speak_publisher = self.speak_publisher

        rospy.sleep(0.1)  # important to make sure the connection to the speaker is established, for next line
        speak("Starting hcm", self.speak_publisher, priority=Speak.HIGH_PRIORITY)

        self.state_machine = HcmStateMachine(dieflag, standupflag, softoff_flag, softstart, start_test,
                                             self.hcm_state_publisher)                    

        rospy.Subscriber("imu/data", Imu, self.update_imu, queue_size=1, tcp_nodelay=True)
        rospy.Subscriber("walking_motor_goals", JointTrajectory, self.walking_goal_callback, queue_size=1, tcp_nodelay=True)
        rospy.Subscriber("animation", AnimationMsg, self.animation_callback, queue_size=1, tcp_nodelay=True)
        rospy.Subscriber("head_motor_goals", JointTrajectory, self.head_goal_callback, queue_size=1, tcp_nodelay=True)
        rospy.Subscriber("record_motor_goals", JointTrajectory, self.record_goal_callback, queue_size=1, tcp_nodelay=True)
        rospy.Subscriber("pause", Bool, self.pause, queue_size=1, tcp_nodelay=True)

        self.animation_action_client = actionlib.SimpleActionClient('animation', PlayAnimationAction)
        BLACKBOARD.animation_client = self.animation_action_client

        self.dyn_reconf = Server(hcm_paramsConfig, self.reconfigure)

        self.main_loop()

    def pause(self, msg):
        """ Updates the pause state for the state machine"""
        BLACKBOARD.penalized = msg.data

    def update_imu(self, msg):
        """Gets new IMU values and computes the smoothed values of these"""
        update_time = rospy.get_time()
        #self.imu_sum += update_time - msg.header.stamp.to_sec()
        self.imu_count += 1
        self.arrt.append(update_time - msg.header.stamp.to_sec())
        self.arrn.append(msg.header.seq)

        self.accel = numpy.array([msg.linear_acceleration.x , msg.linear_acceleration.y, msg.linear_acceleration.z])
        self.gyro = numpy.array([msg.angular_velocity.x, msg.angular_velocity.y , msg.angular_velocity.z])
        self.quaternion = numpy.array(([msg.orientation.x, msg.orientation.y, msg.orientation.z, msg.orientation.w]))

        self.smooth_gyro = numpy.multiply(self.smooth_gyro, 0.95) + numpy.multiply(self.gyro, 0.05)  # gyro
        self.smooth_accel = numpy.multiply(self.smooth_accel, 0.99) + numpy.multiply(self.accel, 0.01)  # accel
        self.not_much_smoothed_gyro = numpy.multiply(self.not_much_smoothed_gyro, 0.5) + numpy.multiply(self.gyro, 0.5)

        BLACKBOARD.raw_gyro = self.gyro
        BLACKBOARD.smooth_gyro = self.smooth_gyro
        BLACKBOARD.not_so_smooth_gyro = self.not_much_smoothed_gyro
        BLACKBOARD.smooth_accel = self.smooth_accel
        BLACKBOARD.quaternion = self.quaternion

        self.last_gyro_update_time = update_time
        BLACKBOARD.last_hardware_update = update_time


    def reconfigure(self, config, level):
        """ Dynamic reconfigure of the fall checker values."""
        # just pass on to the StandupHandler, as all the variables are located there
        BLACKBOARD.fall_checker.update_reconfigurable_values(config, level)
        return config

    def walking_goal_callback(self, msg):
        t = rospy.get_time()
        #self.walk_sum += t - msg.header.stamp.to_sec()
        self.walk_count +=1

        self.aw1.append(t - msg.header.stamp.to_sec())
        self.aw2.append(msg.header.seq)

        BLACKBOARD.walking_active = True
        self.last_walking_update = t
        if self.state_machine.get_current_state() == STATE_CONTROLABLE or \
                        self.state_machine.get_current_state() == STATE_WALKING:
            self.joint_goal_publisher.publish(msg)

    def head_goal_callback(self, msg):
        if self.state_machine.get_current_state() == STATE_CONTROLABLE or self.state_machine.get_current_state() == STATE_WALKING or self.state_machine.get_current_state() == STATE_ANIMATION_RUNNING:
            # we can move our head
            self.joint_goal_publisher.publish(msg)
            # also send it to ros control
            command_msg = trajectory_to_joint_command_msg(msg)
            self.joint_goal_command_publisher.publish(command_msg)

    def record_goal_callback(self, msg):
        if msg is None:
            # record tells us that its finished
            BLACKBOARD.record = False
        else:
            BLACKBOARD.record = True
            self.joint_goal_publisher.publish(msg)
            # also send it to ros control
            command_msg = trajectory_to_joint_command_msg(msg)
            self.joint_goal_command_publisher.publish(command_msg)

    def animation_callback(self, msg):
        """ The animation server is sending us goal positions for the next keyframe"""
        mt = msg.header.stamp.to_sec()
        t = rospy.get_time()
        self.anim_sum += t - mt
        self.anim_count +=1

        BLACKBOARD.last_request = mt
        self.animation_request_time = t
        # VALUES.last_request = msg.header.stamp.to_sec()
        #self.animation_request_time = rospy.get_time()
        if msg.first:
            self.animation_running = True
            BLACKBOARD.external_animation_finished = False
            if msg.hcm:
                # comming from ourselves
                # state machine already know that we're playing it, but we set the value to be sure
                BLACKBOARD.hcm_animation_playing = True
                BLACKBOARD.hcm_animation_finished = False
            else:
                # comming from outside
                if self.state_machine.get_current_state() != STATE_CONTROLABLE:
                    rospy.logwarn("Motion is not controllable, animation refused.")
                    # animation has to wait
                    # state machine should try to become controllable
                    BLACKBOARD.animation_requested = True
                    return
                else:
                    # we're already controllable, go to animation running
                    BLACKBOARD.external_animation_playing = True

        if msg.last:
            if msg.hcm:
                # This was an animation from the state machine
                BLACKBOARD.hcm_animation_playing = False
                BLACKBOARD.hcm_animation_finished = True
            else:
                # this is the last frame, we want to tell the state machine, that we're finished with the animations
                self.animation_running = False
                BLACKBOARD.external_animation_finished = True
                if msg.position is None:
                    # probably this was just to tell us we're finished
                    # we don't need to set another position to the motors
                    return

        # update goal pose
        # self.goal_pose.set_positions(list(msg.state.name), list(msg.state.position))
        # self.goal_pose.set_speeds(list(msg.state.name), list(msg.state.velocity))
        # self.publish_motor_goals()

        # forward positions to cm730, if some where transmitted
        if len(msg.position.points) > 0:
            self.joint_goal_publisher.publish(msg.position)
            # also send it to ros control
            command_msg = trajectory_to_joint_command_msg(msg.position)
            self.joint_goal_command_publisher.publish(command_msg)

    def main_loop(self):
        """ Calls :func:`update_once` until ROS is shutting down """
        iteration = 0
        duration_avg = 0
        start = rospy.get_time()
        rate = rospy.Rate(20)

        while not rospy.is_shutdown():
            finished = self.update_once()
            if finished:
                return

            # Count to get the update frequency
            iteration += 1
            if iteration < 100:
                continue

            if False:  # only for debug
                if duration_avg > 0:
                    duration_avg = 0.5 * duration_avg + 0.5 * (rospy.get_time() - start)
                else:
                    duration_avg = (rospy.get_time() - start)

                # rospy.logwarn("Updates/Sec %f", iteration / duration_avg)
                iteration = 0
                start = rospy.get_time()
            try:
                # catch exeption of moving backwarts in time, when restarting simulator
                rate.sleep()
            except rospy.exceptions.ROSTimeMovedBackwardsException:
                rospy.logwarn("We moved backwards in time. I hope you just resetted the simulation. If not there is something wrong")
            except rospy.exceptions.ROSInterruptException:
                exit()

        # we got external shutdown, tell it to the state machine, it will handle it
        BLACKBOARD.shut_down = True
        # now wait for it finishing the shutdown procedure
        # while not self.state_machine.is_shutdown():
        #    # we still have to update everything
        #    self.update_once()
        #    rospy.sleep(0.01)

        if self.imu_count != 0:
            print("imu mean: " + str((self.imu_sum/self.imu_count)*1000))
        if self.walk_count != 0:
            print("walk mean: " + str((self.walk_sum / self.walk_count)*1000))
        if self.anim_count != 0:
            print("anim mean: " + str((self.anim_sum / self.anim_count)*1000))

        i = 0
        for n in self.arrn:
            self.f.write(str(n) + "," + str(self.arrt[i] * 1000) + "\n")
            i += 1
        self.f.close()

        i = 0
        for n in self.aw2:
            self.f2.write(str(n) + "," + str(self.aw1[i] * 1000) + "\n")
            i += 1
        self.f2.close()

    def update_once(self):
        # check if we're still walking
        if rospy.get_time() - self.last_walking_update > 0.2:
            BLACKBOARD.walking_active = False

        # let statemachine run
        self.state_machine.evaluate()

        # now do corresponding actions depending on state of state machine
        if self.state_machine.is_shutdown():
            # the hcm has to shutdown, we tell main_loop to close the node
            return True


def calculate_robot_angles(raw):
    pitch_angle = calc_sin_angle(raw, numpy.array([0, 1, 0]))
    if raw[2] < 0 and raw[1] < 0:
        pitch_angle = - pitch_angle - 180
    elif raw[2] < 0 and raw[1] > 0:
        pitch_angle = 180 - pitch_angle

    roll_angle = calc_sin_angle(raw, numpy.array([1, 0, 0]))

    yaw_angle = 0

    return -roll_angle, -pitch_angle, yaw_angle


def calc_sin_angle(fst, sec):
    fst_norm = numpy.linalg.norm(fst)
    sec_norm = numpy.linalg.norm(sec)
    if fst_norm == 0 or sec_norm == 0:
        return 0
    return math.degrees(asin(numpy.dot(fst, sec) / (fst_norm * sec_norm)))

def trajectory_to_joint_command_msg(msg):
    command_msg = JointCommand()
    command_msg.joint_names = msg.joint_names
    command_msg.positions = msg.points[0].positions
    if len(msg.points[0].velocities) == len(msg.joint_names):
        command_msg.velocities = msg.points[0].velocities
    else:
        # if vels are not set use max value
        command_msg.velocities = [-1.0] * len(msg.joint_names)
    if len(msg.points[0].velocities) == len(msg.joint_names):
        command_msg.accelerations = msg.points[0].velocities
    else:
        command_msg.accelerations = [-1.0] * len(msg.joint_names)

    #todo this is a hotfix
    #command_msg.velocities = [-1.0] * len(msg.joint_names)
    #command_msg.accelerations = [-1.0] * len(msg.joint_names)
    command_msg.max_currents  = [-1.0] * len(msg.joint_names)
    
    return command_msg

def main():
    """
    parser = argparse.ArgumentParser(description='Start the hcm node')
    parser.add_argument('--nodie', dest='dieflag', action='store_false',
                        help='Supress the autmatical deactivating of the hcm after some time without updates')
    parser.add_argument('--nostandup', dest='standup', action='store_false',
                        help='Surpress automatical stand up')
    parser.add_argument('--softoff', dest='soft', action='store_true',
                        help='Only deactivate motors when robot is not moving')
    parser.add_argument('--softstart', dest='softstart', action='store_true',
                        help='Direclty start in softoff')
    parser.add_argument('--starttest', dest='starttest', action='store_true',
                        help='Ping motors on startup')
    args, unknown = parser.parse_known_args()
    """

    hcm = Motion(dieflag=False, standupflag=True,
                    softoff_flag=True, softstart=False,
                    start_test=False)

if __name__ == "__main__":
    try:
        from bitbots_common.nice import Nice
        nice = Nice()
        nice.set_realtime()
    except ImportError:
        rospy.logwarn("Could not import Nice")
    main()
