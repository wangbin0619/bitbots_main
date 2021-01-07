#include "bitbots_world_model/ObservationModels.h"

LocalObstacleObservationModel::LocalObstacleObservationModel() :
        particle_filter::ObservationModel<PositionStateW>() {}

LocalObstacleObservationModel::~LocalObstacleObservationModel() {}

double
LocalObstacleObservationModel::measure(const PositionStateW& state) const {
    if (last_measurement_.empty()) {
        // ROS_ERROR_STREAM("measure function called with empty measurement
        // list. Prevent this by not calling the function of the particle filter
        // on empty measurements.");
        return 1.0;
    }
    // The maximal value of the measured and the minimal weight.
    return std::max(min_weight_,
            1 / state.calcDistance(*std::min_element(last_measurement_.begin(),
                        last_measurement_.end(),
                        [&state](PositionStateW a, PositionStateW b) {
                            return state.calcDistance(a) <
                                   state.calcDistance(b);
                        })));
}

void LocalObstacleObservationModel::set_measurement(
        std::vector<PositionStateW> measurement) {
    last_measurement_ = measurement;
}

void LocalObstacleObservationModel::set_min_weight(double min_weight) {
    min_weight_ = min_weight;
}

double LocalObstacleObservationModel::get_min_weight() const {
    return min_weight_;
}

void LocalObstacleObservationModel::clear_measurement() {
    last_measurement_.clear();
}

bool LocalObstacleObservationModel::measurements_available() {
    return (!last_measurement_.empty());
}

LocalRobotObservationModel::LocalRobotObservationModel() :
        particle_filter::ObservationModel<PositionState>() {}

LocalRobotObservationModel::~LocalRobotObservationModel() {}

double LocalRobotObservationModel::measure(const PositionState& state) const {
    if (last_measurement_.empty()) {
        // ROS_ERROR_STREAM("measure function called with empty measurement
        // list. Prevent this by not calling the function of the particle filter
        // on empty measurements.");
        return 1.0;
    }
    // The maximal value of the measured and the minimal weight.
    return std::max(min_weight_,
            1 / state.calcDistance(*std::min_element(last_measurement_.begin(),
                        last_measurement_.end(),
                        [&state](PositionState a, PositionState b) {
                            return state.calcDistance(a) <
                                   state.calcDistance(b);
                        })));
}

void LocalRobotObservationModel::set_measurement(
        std::vector<PositionState> measurement) {
    last_measurement_ = measurement;
}

void LocalRobotObservationModel::set_min_weight(double min_weight) {
    min_weight_ = min_weight;
}

double LocalRobotObservationModel::get_min_weight() const {
    return min_weight_;
}

void LocalRobotObservationModel::clear_measurement() {
    last_measurement_.clear();
}

bool LocalRobotObservationModel::measurements_available() {
    return (!last_measurement_.empty());
}


GlobalBallObservationModel::GlobalBallObservationModel() :
        particle_filter::ObservationModel<PositionState>() {}

GlobalBallObservationModel::~GlobalBallObservationModel() {}

double GlobalBallObservationModel::measure(const PositionState& state) const {
    if (last_measurement_.empty()) {
        // ROS_ERROR_STREAM("measure function called with empty measurement
        // list. Prevent this by not calling the function of the particle filter
        // on empty measurements.");
        return 1.0;
    }
    // The maximal value of the measured and the minimal weight.
    return std::max(min_weight_,
            1 / state.calcDistance(*std::min_element(last_measurement_.begin(),
                        last_measurement_.end(),
                        [&state](PositionState a, PositionState b) {
                            return state.calcDistance(a) <
                                   state.calcDistance(b);
                        })));
}

void GlobalBallObservationModel::set_measurement(
        std::vector<PositionState> measurement) {
    last_measurement_ = measurement;
}

void GlobalBallObservationModel::set_min_weight(double min_weight) {
    min_weight_ = min_weight;
}

double GlobalBallObservationModel::get_min_weight() const {
    return min_weight_;
}

void GlobalBallObservationModel::clear_measurement() {
    last_measurement_.clear();
}

bool GlobalBallObservationModel::measurements_available() {
    return (!last_measurement_.empty());
}

GlobalRobotObservationModel::GlobalRobotObservationModel() :
        particle_filter::ObservationModel<PositionState>() {}

GlobalRobotObservationModel::~GlobalRobotObservationModel() {}

double GlobalRobotObservationModel::measure(const PositionState& state) const {
    if (last_measurement_.empty()) {
        // ROS_ERROR_STREAM("measure function called with empty measurement
        // list. Prevent this by not calling the function of the particle filter
        // on empty measurements.");
        return 1.0;
    }
    // The maximal value of the measured and the minimal weight.
    return std::max(min_weight_,
            1 / state.calcDistance(*std::min_element(last_measurement_.begin(),
                        last_measurement_.end(),
                        [&state](PositionState a, PositionState b) {
                            return state.calcDistance(a) <
                                   state.calcDistance(b);
                        })));
}

void GlobalRobotObservationModel::set_measurement(
        std::vector<PositionState> measurement) {
    last_measurement_ = measurement;
}

void GlobalRobotObservationModel::set_min_weight(double min_weight) {
    min_weight_ = min_weight;
}

double GlobalRobotObservationModel::get_min_weight() const {
    return min_weight_;
}

void GlobalRobotObservationModel::clear_measurement() {
    last_measurement_.clear();
}

bool GlobalRobotObservationModel::measurements_available() {
    return (!last_measurement_.empty());
}

LocalFcnnObservationModel::LocalFcnnObservationModel() :
        particle_filter::ObservationModel<PositionState>() {}

LocalFcnnObservationModel::~LocalFcnnObservationModel() {}

double LocalFcnnObservationModel::measure(const PositionState& state) const {
    if (last_measurement_.empty()) {
        // ROS_ERROR_STREAM("measure function called with empty measurement
        // list. Prevent this by not calling the function of the particle filter
        // on empty measurements.");
        return 1.0;
    }
    std::vector<WeightedMeasurement> weighted_measurements;
    for (humanoid_league_msgs::PixelRelative measurement : last_measurement_) {
        weighted_measurements.push_back(WeightedMeasurement{
                state.calcDistance(measurement), measurement.value});
    }
    // put k in a appropriate bounds (in case it is larger than the number of
    // measurements)
    int k = std::min(
            k_, static_cast<int>(
                        weighted_measurements
                                .size()));
    // partly sorting the weighted measurements based on their distance so that
    // the k closest measurements can be selected
    std::nth_element(weighted_measurements.begin(),
            weighted_measurements.begin() + (k - 1),
            weighted_measurements.end(),
            [](WeightedMeasurement& a, WeightedMeasurement& b) {
                return (a.distance < b.distance);
            });
    // summing up the weight to the k measurements
    double weighted_weight = 0;
    for (std::vector<WeightedMeasurement>::iterator it =
                    weighted_measurements.begin();
            it != weighted_measurements.begin() + k; ++it) {
        weighted_weight += it->weight / it->distance;
    }

    // The maximal value of the measured and the minimal weight.
    return std::max(min_weight_, weighted_weight);
}

void LocalFcnnObservationModel::set_measurement(
        humanoid_league_msgs::PixelsRelative measurement) {
    last_measurement_ = measurement.pixels;
}

void LocalFcnnObservationModel::set_min_weight(double min_weight) {
    min_weight_ = min_weight;
}

double LocalFcnnObservationModel::get_min_weight() const {
    return min_weight_;
}

void LocalFcnnObservationModel::set_k(int k) {
    k_ = k;
}

void LocalFcnnObservationModel::clear_measurement() {
    last_measurement_.clear();
}

bool LocalFcnnObservationModel::measurements_available() {
    return (!last_measurement_.empty());
}
