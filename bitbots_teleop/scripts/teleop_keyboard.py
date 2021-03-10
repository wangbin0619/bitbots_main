#!/usr/bin/env python3

# This script was based on the teleop_twist_keyboard package
# original code can be found at https://github.com/ros-teleop/teleop_twist_keyboard
import math
import os

import rospy

from geometry_msgs.msg import Twist
from sensor_msgs.msg import JointState
from bitbots_msgs.msg import JointCommand

import sys, select, termios, tty
import actionlib
from bitbots_msgs.msg import KickGoal, KickAction, KickFeedback
from geometry_msgs.msg import Vector3, Quaternion
from tf.transformations import quaternion_from_euler

msg = """
BitBots Teleop
--------------
Walk around:
    q    w    e
    a    s    d   

q/e: turn left/right
a/d: left/rigth
w/s: forward/back

Move head:
    u    i    o
    j    k    l
    m    ,    .        

k: zero head position
i/,: up/down
j/l: left/right
u/o/m/.: combinations

Controls increase / decrease with multiple presses.
SHIFT increases with factor 10

y: kick left
c: kick right

CTRL-C to quit




"""

moveBindings = {
    'w': (1, 0, 0),
    's': (-1, 0, 0),
    'a': (0, 1, 0),
    'd': (0, -1, 0),
    'q': (0, 0, 1),
    'e': (0, 0, -1),
    'W': (10, 0, 0),
    'S': (-10, 0, 0),
    'A': (0, 10, 0),
    'D': (0, -10, 0),
    'Q': (0, 0, 10),
    'E': (0, 0, -10),
}
headBindings = {
    'u': (1, 1),
    'i': (1, 0),
    'o': (1, -1),
    'j': (0, 1),
    'l': (0, -1),
    'm': (-1, 1),
    ',': (-1, 0),
    '.': (-1, -1),
    'U': (10, 10),
    'I': (10, 0),
    'O': (10, -10),
    'J': (0, 10),
    'L': (0, -10),
    'M': (-10, 10),
    ';': (-10, 0),
    ':': (-10, -10)
}


def getKey():
    tty.setraw(sys.stdin.fileno())
    select.select([sys.stdin], [], [], 0)
    key = sys.stdin.read(1)
    termios.tcsetattr(sys.stdin, termios.TCSADRAIN, settings)
    return key


head_pan_pos = 0
head_tilt_pos = 0


def joint_state_cb(msg):
    global head_pan_pos
    global head_tilt_pos

    if "HeadPan" in msg.name and "HeadTilt" in msg.name:
        head_pan_pos = msg.position[msg.name.index("HeadPan")]
        head_tilt_pos = msg.position[msg.name.index("HeadTilt")]


if __name__ == "__main__":
    settings = termios.tcgetattr(sys.stdin)

    rospy.init_node('teleop_twist_keyboard')

    # Walking Part
    pub = rospy.Publisher('cmd_vel', Twist, queue_size=1, latch=True)

    x_speed_step = 0.01
    y_speed_step = 0.01
    turn_speed_step = 0.01

    x = 0
    y = 0
    th = 0
    status = 0

    # Head Part
    rospy.Subscriber("joint_states", JointState, joint_state_cb, queue_size=1)
    if rospy.get_param("sim_active", default=False):
        head_pub = rospy.Publisher("head_motor_goals", JointCommand, queue_size=1)
    else:
        head_pub = rospy.Publisher("DynamixelController/command", JointCommand, queue_size=1)
    head_msg = JointCommand()
    head_msg.max_currents = [-1] * 2
    head_msg.velocities = [5] * 2
    head_msg.accelerations = [40] * 2
    head_msg.joint_names = ['HeadPan', 'HeadTilt']
    head_msg.positions = [0] * 2

    head_pan_step = 0.05
    head_tilt_step = 0.05

    print(msg)

    goal_left = KickGoal()
    goal_left.header.stamp = rospy.Time.now()
    frame_prefix = "" if os.environ.get("ROS_NAMESPACE") is None else os.environ.get("ROS_NAMESPACE") + "/"
    goal_left.header.frame_id = frame_prefix + "base_footprint"
    goal_left.ball_position.x = 0.2
    goal_left.ball_position.y = 0.1
    goal_left.ball_position.z = 0
    goal_left.kick_direction = Quaternion(*quaternion_from_euler(0, 0, 0))
    goal_left.kick_speed = 1

    goal_right = KickGoal()
    goal_right.header.stamp = rospy.Time.now()
    frame_prefix = "" if os.environ.get("ROS_NAMESPACE") is None else os.environ.get("ROS_NAMESPACE") + "/"
    goal_right.header.frame_id = frame_prefix + "base_footprint"
    goal_right.ball_position.x = 0.2
    goal_right.ball_position.y = -0.1
    goal_right.ball_position.z = 0
    goal_right.kick_direction = Quaternion(*quaternion_from_euler(0, 0, 0))
    goal_right.kick_speed = 1

    client = actionlib.SimpleActionClient('dynamic_kick', KickAction)

    try:
        while (1):
            key = getKey()
            if key in moveBindings.keys():
                x += moveBindings[key][0] * x_speed_step

                x = round(x, 2)
                y += moveBindings[key][1] * y_speed_step
                y = round(y, 2)
                th += moveBindings[key][2] * turn_speed_step
                th = round(th, 2)
            elif key in headBindings.keys():
                head_msg.positions[0] = head_pan_pos + headBindings[key][1] * head_pan_step
                head_msg.positions[1] = head_tilt_pos + headBindings[key][0] * head_tilt_step
                head_pub.publish(head_msg)
            elif key == 'k' or key == 'K':
                # put head back in init
                head_msg.positions[0] = 0
                head_msg.positions[1] = 0
                head_pub.publish(head_msg)
            elif key == 'y':
                # kick left
                client.send_goal(goal_left)
            elif key == 'c':
                # kick right
                print("kick")
                client.send_goal(goal_right)
            else:
                x = 0
                y = 0
                z = 0
                th = 0
                if (key == '\x03'):
                    break

            twist = Twist()
            twist.linear.x = x
            twist.linear.y = y
            twist.linear.z = 0
            twist.angular.x = 0
            twist.angular.y = 0
            twist.angular.z = th
            pub.publish(twist)
            sys.stdout.write("\x1b[A")
            sys.stdout.write("\x1b[A")
            sys.stdout.write("\x1b[A")
            sys.stdout.write("\x1b[A")
            sys.stdout.write("\x1b[A")
            sys.stdout.write("\x1b[A")
            print(
                f"x:    {x}          \ny:    {y}          \nturn: {th}          \n\nhead pan: {head_pan_pos:.2f}          \nhead tilt: {head_tilt_pos:.2f}          ")

    except Exception as e:
        print(e)

    finally:
        print("\n")
        twist = Twist()
        twist.linear.x = 0
        twist.linear.y = 0
        twist.linear.z = 0
        twist.angular.x = 0
        twist.angular.y = 0
        twist.angular.z = 0
        pub.publish(twist)

        termios.tcsetattr(sys.stdin, termios.TCSADRAIN, settings)
