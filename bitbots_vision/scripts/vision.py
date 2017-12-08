#! /usr/bin/env python2


from vision_modules import ball, classifier, lines, live_classifier, horizon, color, debug_image
from humanoid_league_msgs.msg import BallInImage, BallsInImage, LineInformationInImage
from sensor_msgs.msg import Image
from cv_bridge import CvBridge
import rospy
import rospkg
import sys
import cv2
import os


class Vision:

    def __init__(self):
        rospack = rospkg.RosPack()
        package_path = rospack.get_path('bitbots_vision')
        self.field_color_detector = color.PixelListColorDetector(
            package_path +
            rospy.get_param('visionparams/field_color_detector/path'))
        cascade_path = package_path + \
                       rospy.get_param('visionparams/cascade_classifier/path')
        if os.path.exists(cascade_path):
            self.cascade = cv2.CascadeClassifier(cascade_path)
        else:
            print('AAAAHHHH! The specified cascade config file doesn\'t exist!')
        self.ball_classifier = live_classifier\
            .LiveClassifier(package_path +
                            rospy.get_param(
                                'visionparams/classifier/model_path'))
        self.white_color_detector = color.HsvSpaceColorDetector(
            rospy.get_param('visionparams/white_color_detector/lower_values'),
            rospy.get_param('visionparams/white_color_detector/upper_values'))

        # set up ball config
        self.ball_config = {
            'classify_threshold': rospy.get_param('visionparams/ball_finder/classify_threshold'),
            'scale_factor': rospy.get_param('visionparams/ball_finder/scale_factor'),
            'min_neighbors': rospy.get_param('visionparams/ball_finder/min_neighbors'),
            'min_size': rospy.get_param('visionparams/ball_finder/min_size'),
        }

        # set up horizon config
        self.horizon_config = {
            'x_steps': rospy.get_param('visionparams/horizon_finder/horizontal_steps'),
            'y_steps': rospy.get_param('visionparams/horizon_finder/vertical_steps'),
            'prescise_pixel': rospy.get_param('visionparams/horizon_finder/precision_pix'),
            'min_precise_pixel': rospy.get_param('visionparams/horizon_finder/min_precision_pix'),
            'y_offset': rospy.get_param('visionparams/horizon_finder/y_offset')
        }

        # set up lines config
        self.lines_config = {
            'horizon_offset': rospy.get_param('visionparams/line_detector/horizon_offset'),
            'linepoints_range': rospy.get_param('visionparams/line_detector/linepoints_range'),
            'blur_kernel_size': rospy.get_param('visionparams/line_detector/blur_kernel_size'),
        }

        self.debug = False
        # ROS-Stuff:

        # publisher:
        self.pub_balls = rospy.Publisher("ball_in_image", BallsInImage, queue_size=1)
        self.pub_lines = rospy.Publisher("line_in_image", LineInformationInImage, queue_size=5)

        # subscriber:
        self.bridge = CvBridge()
        # TODO: use image_transport
        rospy.Subscriber(rospy.get_param('visionparams/ROS/img_msg_topic'),
                         Image,
                         self._image_callback,
                         rospy.get_param('visionparams/ROS/img_queue_size'))
        rospy.init_node("bitbots_soccer_vision")

        if self.debug:
            rospy.logwarn("Debug windows are enabled")
        else:
            rospy.loginfo("Debug windows are disabled")

        rospy.spin()

    def _image_callback(self, img):
        self.handle_image(img)

    def handle_image(self, image_msg):
        image = self.bridge.imgmsg_to_cv2(image_msg, "bgr8")  # converting the ROS image message to CV2-image

        # setup detectors
        horizon_detector = horizon.HorizonDetector(image,
                                                   self.field_color_detector,
                                                   self.horizon_config)
        line_detector = lines.LineDetector(image,
                                           [],
                                           self.white_color_detector,
                                           horizon_detector,
                                           self.lines_config)
        ball_finder = ball.BallFinder(image, self.cascade, self.ball_config)
        # Todo: filter balls under horizon
        ball_classifier = classifier.Classifier(image, self.ball_classifier,
                                                horizon_detector.candidates_under_horizon(ball_finder.get_candidates(), ))

        # do debug stuff
        if self.debug:
            debug_image_dings = debug_image.DebugImage(image)
            debug_image_dings.draw_horizon(horizon_detector.get_horizon_points())
            debug_image_dings.draw_ball_candidates(horizon_detector.candidates_under_horizon(ball_finder.get_candidates(), 100))
            # debug_image_dings.imshow()

        # create ball msg
        ball_msg = BallsInImage()
        ball_msg.header.frame_id = image_msg.header.frame_id
        ball_msg.header.stamp = image_msg.header.stamp
        if ball_classifier.get_top_candidate() and ball_classifier.get_top_candidate()[1] > 0.5:  # Todo: set real threshold, always send message/only when ball found?
            ball_msg.candidates.append(ball_classifier.get_top_candidate())
        self.pub_balls.publish(ball_msg)

        # create line msg
        line_msg = LineInformationInImage()  # Todo: add lines
        line_msg.header.frame_id = image.header.frame_id
        line_msg.header.stamp = image.header.stamp
        line_msg.segments.append(line_detector.get_linepoints())
        self.pub_lines.publish(line_msg)



if __name__ == "__main__":
    Vision()
