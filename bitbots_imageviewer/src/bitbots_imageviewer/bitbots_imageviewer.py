#!/usr/bin/env python2.7
import copy
import threading
from collections import OrderedDict

import cv2
import os
import rospy
from humanoid_league_msgs.msg import BallInImage, BallsInImage
from sensor_msgs.msg import Image
from cv_bridge import CvBridge, CvBridgeError

pimg = False
lock = threading.Lock()


class Loadimg:
    def __init__(self):
        rospy.init_node("bitbots_imageviewer")

        rospy.Subscriber("/image_raw", Image, self._image_callback, queue_size=1)
        rospy.Subscriber("/rated_balls_in_image", BallsInImage, self._candidates_callback, queue_size=1)

        self.bridge = CvBridge()
        self.images = OrderedDict()
        self.ball_candidates = OrderedDict()

        ibx = 0

        while not rospy.is_shutdown():
            with lock:
                images = copy.deepcopy(self.images)

            #print("Waiting for " + str(self.images.keys()))
            for t in images.keys():  # imgages who are wating
                #print("new image")
                if t in self.ball_candidates:  # Check if all data to draw is there

                    img = images.pop(t)  # get image from queue
                    cans = self.ball_candidates.pop(t)

                    ra = self.bridge.imgmsg_to_cv2(img, "bgr8")
                    if len(cans) > 0:
                        maxcan = max(cans, key=lambda x: x.confidence)
                        for can in cans:
                            i = [0, 0, 0]
                            i[0] = int(can.center.x)
                            i[1] = int(can.center.y)
                            i[2] = int(can.diameter / 2.0)
                            try:
                                if pimg:
                                    i[2] = i[2] + 3
                                    corp = ra[i[1] - i[2] - 3:i[1] + i[2] + 3, i[0] - i[2] - 3:i[0] + i[2] + 3]
                                    cv2.imshow("corp", corp)

                                    corp = cv2.resize(corp, (30, 30), interpolation=cv2.INTER_CUBIC)
                                    corp.reshape((1,) + corp.shape)
                                    ibx +=1
                                if can == maxcan:
                                    c = (255, 0, 0)
                                    if pimg:
                                        cv2.imwrite("pd/nr%d6.jpg" % ibx, corp)
                                elif can.confidence >= 0.5:
                                    c = (0, 255, 0)
                                    if pimg:
                                        cv2.imwrite("pd/nr%d6.jpg" % ibx, corp)
                                else:
                                    c = (0, 0, 255)
                                    if pimg:
                                        cv2.imwrite("nd/nr%d6.jpg" % ibx, corp)
                                        #print(p)
                                    # draw the outer circle
                                cv2.circle(ra, (i[0], i[1]), i[2], c, 2)
                                # draw the center of the circle
                                cv2.circle(ra, (i[0], i[1]), 2, (0, 0, 255), 3)
                            except:
                                pass


                    cv2.imshow("Image", ra)
                    cv2.waitKey(1)
            rospy.sleep(0.01)

    def _image_callback(self, img):
        with lock:
            self.images[img.header.stamp] = img

            if len(self.images) >= 10:
                self.images.popitem(last=False)


    def _candidates_callback(self, balls):
        self.ball_candidates[balls.header.stamp] = balls.candidates
        if len(self.ball_candidates) > 5:
            self.ball_candidates.popitem(last=False)


if __name__ == "__main__":
    Loadimg()
