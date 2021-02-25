//
// Created by judith on 08.03.19.
//

#ifndef BITBOTS_LOCALIZATION_LOCALIZATION_H
#define BITBOTS_LOCALIZATION_LOCALIZATION_H

#include <vector>
#include <memory>
#include <iterator>

#include <ros/ros.h>
#include <std_srvs/Trigger.h>
#include <dynamic_reconfigure/server.h>
#include <Eigen/Core>

#include <visualization_msgs/Marker.h>
#include <visualization_msgs/MarkerArray.h>
#include <geometry_msgs/Point.h>
#include <geometry_msgs/Point32.h>
#include <geometry_msgs/Pose.h>
#include <geometry_msgs/PoseStamped.h>
#include <geometry_msgs/PoseArray.h>
#include <geometry_msgs/PoseWithCovariance.h>
#include <geometry_msgs/PoseWithCovarianceStamped.h>
#include <geometry_msgs/TransformStamped.h>
#include <geometry_msgs/Twist.h>
#include <geometry_msgs/PolygonStamped.h>
#include <sensor_msgs/CameraInfo.h>

#include <tf2_ros/transform_listener.h>
#include <tf2_ros/transform_broadcaster.h>
#include <tf_conversions/tf_kdl.h>
#include <tf2/LinearMath/Quaternion.h>
#include <tf2/LinearMath/Transform.h>
#include <tf2/convert.h>
#include <tf2/utils.h>
#include <tf2_geometry_msgs/tf2_geometry_msgs.h>
#include <tf2_ros/buffer.h>
#include <tf2_ros/message_filter.h>
#include <message_filters/subscriber.h>

#include <particle_filter/ParticleFilter.h>
#include <particle_filter/gaussian_mixture_model.h>
#include <particle_filter/CRandomNumberGenerator.h>

#include <humanoid_league_msgs/LineInformationRelative.h>
#include <humanoid_league_msgs/LineSegmentRelative.h>
#include <humanoid_league_msgs/PoseWithCertaintyArray.h>

#include <bitbots_localization/map.h>
#include <bitbots_localization/LocalizationConfig.h>
#include <bitbots_localization/ObservationModel.h>
#include <bitbots_localization/MotionModel.h>
#include <bitbots_localization/StateDistribution.h>
#include <bitbots_localization/Resampling.h>
#include <bitbots_localization/RobotState.h>

#include <bitbots_localization/reset_filter.h>
#include <bitbots_localization/set_paused.h>
#include <bitbots_localization/tools.h>

#include <cv_bridge/cv_bridge.h>
#include <image_transport/image_transport.h>
#include <sensor_msgs/image_encodings.h>
#include <sensor_msgs/PointCloud2.h>

namespace bl = bitbots_localization;
namespace sm = sensor_msgs;
namespace hlm = humanoid_league_msgs;
namespace gm = geometry_msgs;
namespace pf = particle_filter;


/**
* @class Localization
* @brief Includes the ROS interface, configuration and main loop of the Bit-Bots RoboCup localization.
*/
class Localization {

 public:
  Localization();

  /**
   * Callback for the pause service
   * @param req Request.
   * @param res Response.
   */ 
  bool set_paused_callback( bl::set_paused::Request &req,
                            bl::set_paused::Response &res);

  /**
   * Callback for the filter reset service
   * @param req Request.
   * @param res Response.
   */ 
  bool reset_filter_callback(bl::reset_filter::Request &req,
                             bl::reset_filter::Response &res);

  /**
   * Callback for ros dynamic reconfigure
   * @param config the updated config.
   * @param config_level Not used.
   */ 
  void dynamic_reconfigure_callback(bl::LocalizationConfig &config, uint32_t config_level);

  /**
   * Callback for the line messurements
   * @param msg Message containing the line points.
   */ 
  void LineCallback(const hlm::LineInformationRelative &msg);

  /**
   * Callback for the line point cloud messurements
   * @param msg Message containing the line point cloud.
   */ 
  void LinePointcloudCallback(const sm::PointCloud2 &msg);

  /**
   * Callback for goal posts messurements
   * @param msg Message containing the goal posts.
   */ 
  void GoalPostsCallback(const hlm::PoseWithCertaintyArray &msg); //TODO

  /**
   * Callback for the relative field boundary messurements
   * @param msg Message containing the field boundary points.
   */ 
  void FieldboundaryCallback(const gm::PolygonStamped &msg);

  /**
   * Callback for the field boundary in image the imagespace
   * @param msg Message containing the field boundary points.
   */ 
  void FieldBoundaryInImageCallback(const gm::PolygonStamped &msg);

  /**
   * Resets the state distribution of the state space
   * @param distribution The type of the distribution
   */ 
  void reset_filter(int distribution);

  /**
   * Resets the state distribution of the state space
   * @param distribution The type of the distribution
   * @param x Position of the new state distribution
   * @param y Position of the new state distribution
   */ 
  void reset_filter(int distribution, double x, double y);

  ros::NodeHandle nh_;

 private:
  ros::Subscriber line_subscriber_;
  ros::Subscriber line_point_cloud_subscriber_;
  ros::Subscriber goal_subscriber_;
  ros::Subscriber fieldboundary_subscriber_;
  ros::Subscriber corners_subscriber_;
  ros::Subscriber t_crossings_subscriber_;
  ros::Subscriber crosses_subscriber_;
  ros::Subscriber fieldboundary_in_image_subscriber_;

  ros::Publisher pose_publisher_;
  ros::Publisher pose_with_covariance_publisher_;
  ros::Publisher pose_particles_publisher_;
  ros::Publisher lines_publisher_;
  ros::Publisher line_ratings_publisher_;
  ros::Publisher goal_ratings_publisher_;
  ros::Publisher fieldboundary_ratings_publisher_;
  ros::Publisher corner_ratings_publisher_;
  ros::Publisher t_crossings_ratings_publisher_;
  ros::Publisher crosses_ratings_publisher_;

  ros::ServiceServer reset_service_;
  ros::ServiceServer pause_service_;
  ros::Timer publishing_timer_;
  tf2_ros::Buffer tfBuffer;
  tf2_ros::TransformListener tfListener;
  tf2_ros::TransformBroadcaster br;

  std::shared_ptr<pf::ImportanceResampling<RobotState>> resampling_;
  std::shared_ptr<RobotPoseObservationModel> robot_pose_observation_model_;
  std::shared_ptr<RobotMotionModel> robot_motion_model_;
  //std::shared_ptr<RobotStateDistribution> robot_state_distribution_;
  std::shared_ptr<RobotStateDistributionStartLeft> robot_state_distribution_start_left_;
  std::shared_ptr<RobotStateDistributionStartRight> robot_state_distribution_start_right_;
  std::shared_ptr<RobotStateDistributionRightHalf> robot_state_distribution_right_half_;
  std::shared_ptr<RobotStateDistributionLeftHalf> robot_state_distribution_left_half_;
  std::shared_ptr<RobotStateDistributionPosition> robot_state_distribution_position_;
  std::shared_ptr<RobotStateDistributionPose> robot_state_distribution_pose_;
  std::shared_ptr<particle_filter::ParticleFilter<RobotState>> robot_pf_;
  RobotState estimate_;
  std::vector<double> estimate_cov_;

  bool resampled_ = false;

  hlm::LineInformationRelative line_information_relative_;
  sm::PointCloud2 line_pointcloud_relative_;
  hlm::PoseWithCertaintyArray goal_posts_relative_;
  gm::PolygonStamped fieldboundary_relative_;
  sm::CameraInfo cam_info_;
  std::vector<gm::PolygonStamped> fieldboundary_in_image_;

  ros::Time last_stamp_lines = ros::Time(0);
  ros::Time last_stamp_lines_pc = ros::Time(0);
  ros::Time last_stamp_goals = ros::Time(0);
  ros::Time last_stamp_fb_points = ros::Time(0);

  std::vector<gm::Point32> interpolateFieldboundaryPoints(gm::Point32 point1, gm::Point32 point2);

  /**
   * Runs the filter for one step
   * @param e ROS Timer event 
   */ 
  void run_filter_one_step(const ros::TimerEvent &e);

  std::shared_ptr<Map> lines_;
  std::shared_ptr<Map> goals_;
  std::shared_ptr<Map> field_boundary_;
  std::shared_ptr<Map> corner_;
  std::shared_ptr<Map> t_crossings_map_;
  std::shared_ptr<Map> crosses_map_;

  gmms::GaussianMixtureModel pose_gmm_;
  std::vector<gm::Point32> line_points_;
  particle_filter::CRandomNumberGenerator random_number_generator_;
  bl::LocalizationConfig config_;
  std_msgs::ColorRGBA marker_color;
  bool first_configuration_ = true;

  /**
   * Publishes the position as a transform 
   */ 
  void publish_transforms();

  /**
   * Publishes the position as a message 
   */ 
  void publish_pose_with_covariance();

  /**
   * Debug publisher
   */ 
  void publish_debug();

  /**
   * Publishes the visualization markers for each particle
   */ 
  void publish_particle_markers();

  /**
   * Publishes the rating visualizations for each meassurement class
   */ 
  void publish_ratings();

  /**
   * Publishes the rating visualizations for a abitray messurement class
   * @param measurements all measurements of the messurement class
   * @param scale scale of the markers
   * @param name name of the class
   * @param map map for this class
   * @param publisher ros publisher for the type visualization_msgs::Marker
   */ 
  void publish_debug_rating(
    std::vector<std::pair<double, double>>    measurements,
    double scale,
    const char name[24],
    std::shared_ptr<Map> map,
    ros::Publisher &publisher);

  /**
   * Updates the messurements for all classes
   */ 
  void updateMeasurements();

  /**
   * Gets the and convert motion / odometry information 
   */ 
  void getMotion();

  geometry_msgs::TransformStamped transformOdomBaseLink;
  bool initialization = true;

  geometry_msgs::Vector3 linear_movement_;
  geometry_msgs::Vector3 rotational_movement_;
  bool new_linepoints_ = false;
  bool robot_moved = false;
  int timer_callback_count_ = 0;
};

#endif //BITBOTS_LOCALIZATION_LOCALIZATION_H
