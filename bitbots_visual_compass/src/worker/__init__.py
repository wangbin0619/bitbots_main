#!/usr/bin/env python3

from __future__ import absolute_import

# import rospy

from .interface import VisualCompass as VisualCompassInterface
from .binary import BinaryCompass
from .multiple import MultipleCompass


class VisualCompass(VisualCompassInterface):
    def __init__(self, config):
        self.compass = None
        self.compassType = None
        self.compassClasses = {
            "binary": BinaryCompass,
            "multiple": MultipleCompass
        }

        self.set_config(config)

    def process_image(self, image, resultCB=None, debugCB=None):
        return self.compass.process_image(image, resultCB, debugCB)

    def set_config(self, config):
        compass_type = config['compass_type']
        if compass_type == self.compassType:
            self.compass.set_config(config)
        else:
            self.compassType = compass_type
            if compass_type not in self.compassClasses:
                raise AssertionError(self.compassType + ": Compass not available!")
            compass_class = self.compassClasses[self.compassType]
            self.compass = compass_class(config)
            #rospy.loginfo("Compass type: %(compass_type)s is loaded." % {'compass_type': self.compassType})

    def set_truth(self, angle, image):
        return self.compass.set_truth(angle, image)

    def get_feature_map(self):
        return self.compass.get_feature_map()

    def set_feature_map(self, feature_map):
        self.compass.set_feature_map(feature_map)

    def get_mean_feature_count(self):
        if self.compassType == "multiple":
            return self.compass.get_mean_feature_count()

    def set_mean_feature_count(self, mean_feature_count):
        if self.compassType == "multiple":
            return self.compass.set_mean_feature_count(mean_feature_count)

    def get_side(self):
        return self.compass.get_side()







