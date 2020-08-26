//
// Created by judith on 09.03.19.
//


#include <bitbots_localization/ObservationModel.h>

RobotPoseObservationModel::RobotPoseObservationModel(std::shared_ptr<Map> map_lines,
                                                     std::shared_ptr<Map> map_goals,
                                                     std::shared_ptr<Map> map_field_boundary,
                                                     std::shared_ptr<Map> map_corners,
                                                     std::shared_ptr<Map> map_t_crossings,
                                                     std::shared_ptr<Map> map_crosses,
                                                     bl::LocalizationConfig &config)
    : particle_filter::ObservationModel<RobotState>() {
  map_lines_ = map_lines;
  map_goals_ = map_goals;
  map_field_boundary_ = map_field_boundary;
  map_corners_ = map_corners;
  map_t_crossings_ = map_t_crossings;
  map_crosses_ = map_crosses;
  config_ = config;
  particle_filter::ObservationModel<RobotState>::accumulate_weights_ = true;

}

double RobotPoseObservationModel::calculate_weight_for_class(
    const RobotState &state,
    const std::vector<std::pair<double, double>> &last_measurement,
    std::shared_ptr<Map> map) const {
  double particle_weight_for_class = 0;
  if (!last_measurement.empty()) {
    number_of_effective_measurements_ += 1;
    std::vector<double> ratings = map->Map::provideRating(state, last_measurement);
    for (double rating : ratings) {
      particle_weight_for_class += rating;
    }
    particle_weight_for_class /= ratings.size();
  }
  return particle_weight_for_class;
}

double RobotPoseObservationModel::measure(const RobotState &state) const {
  number_of_effective_measurements_ = 0;

  double particle_weight_lines = calculate_weight_for_class(state, last_measurement_lines_, map_lines_);
  double particle_weight_goal = calculate_weight_for_class(state, last_measurement_goal_, map_goals_);
  double particle_weight_field_boundary = calculate_weight_for_class(state, last_measurement_field_boundary_, map_field_boundary_);
  double particle_weight_corners = calculate_weight_for_class(state, last_measurement_corners_, map_corners_);
  double particle_weight_t_crossings = calculate_weight_for_class(state, last_measurement_t_crossings_, map_t_crossings_);
  double particle_weight_crosses = calculate_weight_for_class(state, last_measurement_crosses_, map_crosses_);

  number_lines = last_measurement_lines_.size();
  number_goals = last_measurement_goal_.size();
  number_fb_points = last_measurement_field_boundary_.size();
  number_corners = last_measurement_corners_.size();
  number_tcrossings = last_measurement_t_crossings_.size();
  number_crosses = last_measurement_crosses_.size();

  double weight = (number_of_effective_measurements_ == 0) ? 0 : (
      ( particle_weight_lines * config_.lines_factor +
        particle_weight_goal * config_.goals_factor +
        particle_weight_field_boundary * config_.field_boundary_factor +
        particle_weight_corners * config_.corners_factor +
        particle_weight_t_crossings * config_.t_crossings_factor +
        particle_weight_crosses * config_.crosses_factor) /
          number_of_effective_measurements_); // TODO evaluate this devision

  if (weight < min_weight_) {
    weight = min_weight_;
  }

  return weight; //exponential?
}

void RobotPoseObservationModel::set_measurement_lines(hlm::LineInformationRelative measurement) {
  // convert to polar
  for (hlm::LineSegmentRelative &segment : measurement.segments) {
    std::pair<double, double> linePolar = cartesianToPolar(segment.start.pose.position.x, segment.start.pose.position.y);
    last_measurement_lines_.push_back(linePolar);
  }
}

void RobotPoseObservationModel::set_measurement_goal(hlm::PoseWithCertaintyArray measurement) {
  // convert to polar
  for (hlm::PoseWithCertainty &post : measurement.poses) {
    std::pair<double, double> postPolar = cartesianToPolar(post.pose.pose.position.x, post.pose.pose.position.y);
    last_measurement_goal_.push_back(postPolar);
  }
}

void RobotPoseObservationModel::set_measurement_field_boundary(gm::PolygonStamped measurement) {
  // convert to polar
  for (gm::Point32 &point : measurement.polygon.points) {
    std::pair<double, double> fieldBoundaryPointPolar = cartesianToPolar(point.x,
                                                                         point.y); // z is 0
    last_measurement_field_boundary_.push_back(fieldBoundaryPointPolar);
  }
}

void RobotPoseObservationModel::set_measurement_corners(hlm::LineInformationRelative measurement) {
  // convert to polar
  for (hlm::LineIntersectionRelative &intersection : measurement.intersections) {
    if (intersection.type == intersection.L)
    {
      std::pair<double, double> cornerPolar = cartesianToPolar(intersection.pose.pose.pose.position.x, intersection.pose.pose.pose.position.y); // z is 0
      last_measurement_corners_.push_back(cornerPolar);
    }
  }
}

void RobotPoseObservationModel::set_measurement_t_crossings(hlm::LineInformationRelative measurement) {
  // convert to polar
  for (hlm::LineIntersectionRelative &intersection : measurement.intersections) {
    if (intersection.type == intersection.T)
    {
      std::pair<double, double> cornerPolar = cartesianToPolar(intersection.pose.pose.pose.position.x, intersection.pose.pose.pose.position.y); // z is 0
      last_measurement_t_crossings_.push_back(cornerPolar);
    }
  }
}

void RobotPoseObservationModel::set_measurement_crosses(hlm::LineInformationRelative measurement) {
  // convert to polar
  for (hlm::LineIntersectionRelative &intersection : measurement.intersections) {
    if (intersection.type == intersection.X)
    {
      std::pair<double, double> cornerPolar = cartesianToPolar(intersection.pose.pose.pose.position.x, intersection.pose.pose.pose.position.y); // z is 0
      last_measurement_crosses_.push_back(cornerPolar);
    }
  }
}

std::vector<std::pair<double, double>> RobotPoseObservationModel::get_measurement_lines() const {
  return last_measurement_lines_;
}

std::vector<std::pair<double, double>> RobotPoseObservationModel::get_measurement_goals() const {
  return last_measurement_goal_;
}

std::vector<std::pair<double, double>> RobotPoseObservationModel::get_measurement_field_boundary() const {
  return last_measurement_field_boundary_;
}

std::vector<std::pair<double, double>> RobotPoseObservationModel::get_measurement_corners() const {
  return last_measurement_corners_;
}

std::vector<std::pair<double, double>> RobotPoseObservationModel::get_measurement_t_crossings() const {
  return last_measurement_t_crossings_;
}

std::vector<std::pair<double, double>> RobotPoseObservationModel::get_measurement_crosses() const {
  return last_measurement_crosses_;
}

void RobotPoseObservationModel::set_min_weight(double min_weight) {
  min_weight_ = min_weight;
}

double RobotPoseObservationModel::get_min_weight() const {
  return min_weight_;
}

void RobotPoseObservationModel::clear_measurement() {
  last_measurement_lines_.clear();
  last_measurement_goal_.clear();
  last_measurement_field_boundary_.clear();
  last_measurement_corners_.clear();
  last_measurement_t_crossings_.clear();
  last_measurement_crosses_.clear();
}

bool RobotPoseObservationModel::measurements_available() {
  bool available = false;
  available |= !last_measurement_lines_.empty();
  available |= !last_measurement_goal_.empty();
  available |= !last_measurement_field_boundary_.empty();
  available |= !last_measurement_corners_.empty();
  available |= !last_measurement_t_crossings_.empty();
  available |= !last_measurement_crosses_.empty();
  return available;
}

