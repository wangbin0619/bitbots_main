#ifndef BITBOTS_SPLINES_INCLUDE_BITBOTS_SPLINES_POSITION_SPLINE_H_
#define BITBOTS_SPLINES_INCLUDE_BITBOTS_SPLINES_POSITION_SPLINE_H_

#include <bitbots_splines/smooth_spline.h>
#include <bitbots_splines/spline_container.h>
#include <tf2/LinearMath/Vector3.h>

#include <geometry_msgs/msg/point.hpp>

namespace bitbots_splines {

class PositionSpline {
 public:
  geometry_msgs::msg::Point getGeometryMsgPosition(double time);

  tf2::Vector3 getPos(double time);
  tf2::Vector3 getVel(double time);
  tf2::Vector3 getAcc(double time);

  SmoothSpline *x();
  SmoothSpline *y();
  SmoothSpline *z();

 private:
  SmoothSpline x_;
  SmoothSpline y_;
  SmoothSpline z_;
};
}  // namespace bitbots_splines
#endif  // BITBOTS_SPLINES_INCLUDE_BITBOTS_SPLINES_POSITION_SPLINE_H_
