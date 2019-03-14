#include <bitbots_world_model/ObstacleStates.h>

PositionState::PositionState() :
    xPos_(0.0),
    yPos_(0.0) {}

PositionState::PositionState(float x, float y) :
    xPos_(x),
    yPos_(y) {}

PositionState PositionState::operator*(float factor) const {
    return(PositionState(getXPos() * factor, getYPos() * factor));
}

PositionState& PositionState::operator+=(const PositionState& other) {
    setXPos(getXPos() + other.getXPos());
    setYPos(getYPos() + other.getYPos());
    return *this;
}

PositionState& PositionState::operator=(const PositionState& other) {
  if (this != &other) {
    setXPos(other.getXPos());
    setYPos(other.getYPos());
  }
  return *this;
}

PositionState::~PositionState() {}

void PositionState::setXPos(float x) {
    xPos_ = x;
}

void PositionState::setYPos(float y) {
    yPos_ = y;
}

float PositionState::getXPos() const {
    return xPos_;
}

float PositionState::getYPos() const {
    return yPos_;
}

visualization_msgs::Marker PositionState::renderMarker(particle_filter::ParticleFilter<PositionState>::ParticleList& particle_list, std_msgs::ColorRGBA color, ros::Duration lifetime, std::string n_space) {
    visualization_msgs::Marker msg;
    msg.header.stamp = ros::Time::now();
    msg.header.frame_id = "/base_link";
    msg.action = visualization_msgs::Marker::ADD;
    msg.type = visualization_msgs::Marker::POINTS;
    msg.pose.orientation.w = 1;
    msg.scale.x = 0.05;
    msg.scale.y = 0.05;
    msg.scale.z = 0.05;
    msg.color = color;
    msg.lifetime = lifetime;
    msg.ns = n_space;
    for (particle_filter::Particle<PositionState> *particle : particle_list) {
        geometry_msgs::Point point_msg;
        point_msg.x = particle->getState().getXPos();
        point_msg.y = particle->getState().getYPos();
        msg.points.push_back(point_msg);
    }
    return msg;
}

visualization_msgs::Marker PositionState::renderMarker(PositionState particle_state, std_msgs::ColorRGBA color, ros::Duration lifetime, std::string n_space) {
    visualization_msgs::Marker msg;
    msg.header.stamp = ros::Time::now();
    msg.header.frame_id = "/base_link";
    msg.action = visualization_msgs::Marker::ADD;
    msg.type = visualization_msgs::Marker::POINTS;
    msg.pose.orientation.w = 1;
    msg.scale.x = 0.05;
    msg.scale.y = 0.05;
    msg.scale.z = 0.05;
    msg.color = color;
    msg.lifetime = lifetime;
    msg.ns = n_space;
    geometry_msgs::Point point_msg;
    point_msg.x = particle_state.getXPos();
    point_msg.y = particle_state.getYPos();
    msg.points.push_back(point_msg);
    return msg;
}

double PositionState::calcDistance(const PositionState& state) const {
    // TODO
    double diff =  std::sqrt(std::pow(getXPos() - state.getXPos(), 2) + std::pow(getYPos() - state.getYPos(), 2));
    if (diff == 0.0) {
        diff = 0.0001;
    }
    return diff;
}

double PositionState::calcDistance(const humanoid_league_msgs::PixelRelative &pixel) const {
    // TODO
    double diff =  std::sqrt(std::pow(getXPos() - pixel.position.x, 2) + std::pow(getYPos() - pixel.position.y, 2));
    if (diff == 0.0) {
        diff = 0.0001;
    }
    return diff;
}

void PositionState::convertParticleListToEigen(
        const std::vector<particle_filter::Particle<PositionState>*>& particle_list,
        Eigen::MatrixXd& matrix) {
    matrix.resize(particle_list.size(), 2);
    for(int i = 0; i < particle_list.size(); i++) {
        matrix(i, 0) = particle_list[i]->getState().getXPos();
        matrix(i, 1) = particle_list[i]->getState().getYPos();
    }
}



PositionStateW::PositionStateW() :
    PositionState(),
    width_(0.0) {}

PositionStateW::PositionStateW(float x, float y, float w) :
    PositionState(x, y),
    width_(w) {}

PositionStateW PositionStateW::operator*(float factor) const {
    return(PositionStateW(getXPos() * factor, getYPos() * factor, getWidth() * factor));
}

PositionStateW& PositionStateW::operator+=(const PositionStateW& other) {
    setXPos(getXPos() + other.getXPos());
    setYPos(getYPos() + other.getYPos());
    setWidth(getWidth() + other.getWidth());
    return *this;
}

PositionStateW& PositionStateW::operator=(const PositionStateW& other) {
  if (this != &other) {
    setXPos(other.getXPos());
    setYPos(other.getYPos());
    setWidth(other.getWidth());
  }
  return *this;
}

PositionStateW::~PositionStateW() {}

void PositionStateW::setWidth(float w) {
    width_ = w;
}

float PositionStateW::getWidth() const {
    return width_;
}

visualization_msgs::Marker PositionStateW::renderMarker(particle_filter::ParticleFilter<PositionStateW>::ParticleList& particle_list, std_msgs::ColorRGBA color, ros::Duration lifetime, std::string n_space) {
    visualization_msgs::Marker msg;
    msg.header.stamp = ros::Time::now();
    msg.header.frame_id = "/base_link";
    msg.action = visualization_msgs::Marker::ADD;
    msg.type = visualization_msgs::Marker::POINTS;
    msg.pose.orientation.w = 1;
    msg.scale.x = 0.05;
    msg.scale.y = 0.05;
    msg.scale.z = 0.05;
    msg.color = color;
    msg.lifetime = lifetime;
    msg.ns = n_space;
    for (particle_filter::Particle<PositionStateW> *particle : particle_list) {
        geometry_msgs::Point point_msg;
        point_msg.x = particle->getState().getXPos();
        point_msg.y = particle->getState().getYPos();
        msg.points.push_back(point_msg);
    }
    return msg;
}

double PositionStateW::calcDistance(const PositionStateW& state) const {
    // TODO
    double diff =  std::sqrt(std::pow(getXPos() - state.getXPos(), 2) + std::pow(getYPos() - state.getYPos(), 2));
    if (diff == 0.0) {
        diff = 0.0001;
    }
    return diff;
}

void PositionStateW::convertParticleListToEigen(
        const std::vector<particle_filter::Particle<PositionStateW>*>& particle_list,
        Eigen::MatrixXd& matrix) {
    matrix.resize(particle_list.size(), 3);
    for(int i = 0; i < particle_list.size(); i++) {
        matrix(i, 0) = particle_list[i]->getState().getXPos();
        matrix(i, 1) = particle_list[i]->getState().getYPos();
        matrix(i, 2) = particle_list[i]->getState().getWidth();
    }
}

PoseState::PoseState() :
    PositionState(),
    orientation_(0.0) {}

PoseState::PoseState(float x, float y, float o) :
    PositionState(x, y),
    orientation_(o) {}

PoseState PoseState::operator*(float factor) const {
    return(PoseState(getXPos() * factor, getYPos() * factor, getOrientation() * factor));
}

PoseState& PoseState::operator+=(const PoseState& other) {
    setXPos(getXPos() + other.getXPos());
    setYPos(getYPos() + other.getYPos());
    setOrientation(getOrientation() + other.getOrientation());
    return *this;
}

PoseState& PoseState::operator=(const PoseState& other) {
  if (this != &other) {
    setXPos(other.getXPos());
    setYPos(other.getYPos());
    setOrientation(other.getOrientation());
  }
  return *this;
}

PoseState::~PoseState() {}

void PoseState::setOrientation(float o) {
    orientation_ = o;
}

float PoseState::getOrientation() const {
    return orientation_;
}

double PoseState::calcDistance(const PoseState& state) const {
    // TODO
    double diff =  std::sqrt(std::pow(getXPos() - state.getXPos(), 2) + std::pow(getYPos() - state.getYPos(), 2));
    if (diff == 0.0) {
        diff = 0.0001;
    }
    return diff;
}

void PoseState::convertParticleListToEigen(
        const std::vector<particle_filter::Particle<PoseState>*>& particle_list,
        Eigen::MatrixXd& matrix) {
    matrix.resize(particle_list.size(), 3);
    for(int i = 0; i < particle_list.size(); i++) {
        matrix(i, 0) = particle_list[i]->getState().getXPos();
        matrix(i, 1) = particle_list[i]->getState().getYPos();
        matrix(i, 2) = particle_list[i]->getState().getOrientation();  // TODO
    }
}

