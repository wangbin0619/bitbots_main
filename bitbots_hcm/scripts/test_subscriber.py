#!/usr/bin/env python3
import rclpy
from rclpy.node import Node
import time
from sensor_msgs.msg import Imu

class SubTest():

    def __init__(self):
        rclpy.init(args=None)
        self.arrt = []
        self.arrn = []
        self.sum =0
        self.count=0
        self.max = 0
        self.sub = rospy.Subscriber("test", Imu, self.cb, queue_size=1)
        self.f = open("latencies", 'w')

        while rclpy.ok():
            time.sleep(1)
        if self.count !=0:
            print("mean: " + str((self.sum/self.count)*1000))
        print("max: " + str(self.max*1000))

        i = 0
        for n in self.arrn:
            self.f.write(str(n) + "," + str(self.arrt[i]*1000) + "\n")
            i+=1
        self.f.close()

    def cb(self, msg:Imu):
        diff = float(self.get_clock().now().seconds_nanoseconds()[0] + self.get_clock().now().seconds_nanoseconds()[1]/1e9) - msg.header.stamp.to_sec()
        self.arrt.append(diff)
        self.arrn.append(msg.header.seq)
        self.sum += diff
        self.count +=1
        self.max = max(self.max, diff)





if __name__ == "__main__":
    SubTest()
