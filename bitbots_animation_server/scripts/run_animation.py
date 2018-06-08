#!/usr/bin/env python3
# -*- coding: utf8 -*-
import actionlib
import rospy
import sys

import humanoid_league_msgs.msg
import time


def anim_run(anim=None):
    anim_client = actionlib.SimpleActionClient('animation', humanoid_league_msgs.msg.PlayAnimationAction)
    rospy.init_node('anim_sender', anonymous=False)
    if anim is None:
        anim = rospy.get_param("~anim")
    if anim is None or anim == "":
        rospy.logwarn("Tried to play an animation with an empty name!")
        return False
    first_try = anim_client.wait_for_server(
        rospy.Duration(rospy.get_param("hcm/anim_server_wait_time", 10)))
    if not first_try:
        rospy.logerr(
            "Animation Action Server not running! Motion can not work without animation action server. "
            "Will now wait until server is accessible!")
        anim_client.wait_for_server()
        rospy.logwarn("Animation server now running, hcm will go on.")
    goal = humanoid_league_msgs.msg.PlayAnimationGoal()
    goal.animation = anim
    goal.hcm = True  # the animation is from the hcm
    print(anim_client.send_goal_and_wait(goal))
    rospy.sleep(0.5)

if __name__ == '__main__':
    # run with "rosrun bitbots_animation_server run_animation.py NAME"
    #print("Deactivate evaluation of state machine in hcm befor using this, maybe")
    if len(sys.argv) > 1:
        # Support for _anim:=NAME -style execution for legacy reasons
        if sys.argv[1].startswith('_anim:=') or sys.argv[1].startswith('anim:='):
            anim_run(sys.argv[1].split(':=')[1])
        else:
            anim_run(sys.argv[1])
