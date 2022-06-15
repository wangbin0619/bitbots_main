from typing import Dict, Union, List, Tuple

import cv2
import rclpy
from rclpy.node import Node
from sensor_msgs.msg import Image
from humanoid_league_msgs.msg import Audio, GameState
from soccer_vision_2d_msgs.msg import BallArray, FieldBoundary, GoalpostArray, RobotArray, Robot
from bitbots_vision.vision_modules import lines, field_boundary, color, debug, \
    obstacle, yolo_handler, ros_utils, candidate

from bitbots_vision.vision_modules import yoeo_handler

from abc import ABC, abstractmethod

logger = rclpy.logging.get_logger('yoeo_vision_components')


class DebugImageColors:
    # BGR
    ball = (0, 255, 0)  # green
    team_mates = (153, 51, 255)  # magenta
    opponents = (255, 255, 102)  # cyan
    misc_obstacles = (160, 160, 160)  # grey
    goalposts = (255, 255, 255)  # white
    field_boundary = (0, 0, 255)  # red
    field_boundary_convex = (0, 255, 255)  # yellow
    lines = (255, 0, 0)  # blue


class DebugImageFactory:
    _config: Dict = {}
    _debug_image: Union[None, debug.DebugImage] = None

    @classmethod
    def create(cls, config: Dict) -> debug.DebugImage:
        if cls._new_debug_image_has_to_be_created(config):
            cls._create_new_debug_image(config)
        return cls._debug_image

    @classmethod
    def _new_debug_image_has_to_be_created(cls, config: Dict) -> bool:
        return cls._debug_image is None \
               or ros_utils.config_param_change(cls._config, config, 'component_debug_image_active')

    @classmethod
    def _create_new_debug_image(cls, config: Dict) -> None:
        cls._debug_image = debug.DebugImage(config['component_debug_image_active'])
        cls._config = config


class YOEOFieldBoundaryDetectorFactory:
    _field_boundary_detector: Union[None, field_boundary.FieldBoundaryDetector] = None
    _field_boundary_detector_search_method: Union[None, str] = None
    _yoeo_id: Union[None, int] = None

    @classmethod
    def create(cls, config: Dict, yoeo: yoeo_handler.IYOEOHandler) -> field_boundary.FieldBoundaryDetector:
        if cls._new_field_boundary_detector_has_to_be_created(config, yoeo):
            cls._create_new_field_boundary_detector(config, yoeo)
        return cls._field_boundary_detector

    @classmethod
    def _new_field_boundary_detector_has_to_be_created(cls, config: Dict, yoeo: yoeo_handler.IYOEOHandler) -> bool:
        return cls._field_boundary_detector is None \
               or cls._field_boundary_detector_search_method != config['field_boundary_detector_search_method'] \
               or cls._yoeo_id != id(yoeo)

    @classmethod
    def _create_new_field_boundary_detector(cls, config: Dict, yoeo: yoeo_handler.IYOEOHandler) -> None:
        field_boundary_detector_class = field_boundary.FieldBoundaryDetector.get_by_name(
            config['field_boundary_detector_search_method']
        )
        field_detector = yoeo_handler.YOEOFieldSegmentation(yoeo)

        cls._field_boundary_detector = field_boundary_detector_class(config, field_detector)
        cls._field_boundary_detector_search_method = config['field_boundary_detector_search_method']
        cls._yoeo_id = id(yoeo)


class YOEOObstacleDetectorFactory:
    _config: Dict = {}
    _blue_color_detector: Union[None, color.HsvSpaceColorDetector] = None
    _red_color_detector: Union[None, color.HsvSpaceColorDetector] = None
    _robot_detector = None
    _yoeo_id: Union[None, int] = None

    @classmethod
    def create(cls,
               config: Dict,
               yoeo: yoeo_handler.IYOEOHandler,
               color: Union[None, int] = None,
               subtractors: Union[None, List[obstacle.ColorObstacleDetector]] = None) \
            -> obstacle.ColorObstacleDetector:
        if cls._new_robot_detector_has_to_be_created(yoeo):
            cls._create_new_robot_detector(yoeo)

        if cls._new_red_color_detector_has_to_be_created(config):
            cls._create_new_red_color_detector(config)

        if cls._new_blue_color_detector_has_to_be_created(config):
            cls._create_new_blue_color_detector(config)

        color_detector = cls._select_color_detector_based_on(color)

        return obstacle.ColorObstacleDetector(
            cls._robot_detector,
            color_detector,
            threshold=config['obstacle_color_threshold'],
            subtractors=subtractors)

    @classmethod
    def _new_robot_detector_has_to_be_created(cls, yoeo: yoeo_handler.IYOEOHandler) -> bool:
        return cls._robot_detector is None or cls._yoeo_id != id(yoeo)

    @classmethod
    def _create_new_robot_detector(cls, yoeo: yoeo_handler.IYOEOHandler) -> None:
        cls._robot_detector = yoeo_handler.YOEORobotDetector(yoeo)
        cls._yoeo_id = id(yoeo)

    @classmethod
    def _new_red_color_detector_has_to_be_created(cls, config: Dict) -> bool:
        return ros_utils.config_param_change(cls._config, config, r'^red_color_detector_')

    @classmethod
    def _create_new_red_color_detector(cls, config) -> None:
        cls._red_color_detector = color.HsvSpaceColorDetector(config, "red")

    @classmethod
    def _new_blue_color_detector_has_to_be_created(cls, config: Dict) -> bool:
        return ros_utils.config_param_change(cls._config, config, r'^blue_color_detector_')

    @classmethod
    def _create_new_blue_color_detector(cls, config) -> None:
        cls._blue_color_detector = color.HsvSpaceColorDetector(config, "blue")

    @classmethod
    def _select_color_detector_based_on(cls, color: Union[None, int]):
        if color == GameState.BLUE:
            color_detector = cls._blue_color_detector
        elif color == GameState.RED:
            color_detector = cls._red_color_detector
        else:
            color_detector = None
        return color_detector


class IVisionComponent(ABC):
    @abstractmethod
    def configure(self, config: Dict, yoeo: yoeo_handler.IYOEOHandler) -> None:
        ...

    @abstractmethod
    def run(self, image_msg) -> None:
        """
        :param image_msg: Image message
        :type image_msg:  sensor_msgs.msg._image.Image
        """
        ...

    @abstractmethod
    def set_image(self, image) -> None:
        """
        :param image: Image to run the component on
        :type image:  numpy nd.array (height, width, channels)
        """
        ...


class CameraCapCheckComponent(IVisionComponent):
    """
    Component checks if the camera cap could still be attached to the camera.
    Component deactivates itself after the first image.
    """

    def __init__(self, node: Node):
        self._camera_cap_brightness_threshold: int = 0
        self._config: Dict = {}
        self._image = None  # numpy nd.array (height, width, channels)
        self._is_first_image: bool = True
        self._node: Node = node
        self._publisher: Union[None, rclpy.publisher.Publisher] = None

    def configure(self, config: Dict, yoeo: yoeo_handler.IYOEOHandler) -> None:
        self._camera_cap_brightness_threshold = config['vision_blind_threshold']

        self._register_publisher(config)
        self._config = config

    def _register_publisher(self, new_config: Dict) -> None:
        self._publisher = ros_utils.create_or_update_publisher(
            self._node,
            self._config,
            new_config,
            self._publisher,
            'ROS_audio_msg_topic',
            Audio
        )

    def run(self, image_msg) -> None:
        """
        :param image_msg: Image message
        :type image_msg:  sensor_msgs.msg._image.Image
        """
        if self._component_has_not_run_yet():
            self._set_component_has_run()
            self._run_component()

    def _component_has_not_run_yet(self) -> bool:
        return self._is_first_image

    def _set_component_has_run(self) -> None:
        self._is_first_image = False

    def _run_component(self) -> None:
        if self._camera_cap_could_be_on():
            self._log_error()
            self._issue_oral_warning()

    def _camera_cap_could_be_on(self) -> bool:
        mean_image_brightness = self._calculate_mean_image_brightness()
        return mean_image_brightness < self._camera_cap_brightness_threshold

    def _calculate_mean_image_brightness(self) -> float:
        return sum(cv2.mean(self._image))

    @staticmethod
    def _log_error() -> None:
        logger.error("Image is too dark! Camera cap not removed?")

    def _issue_oral_warning(self) -> None:
        ros_utils.speak("Hey!   Remove my camera cap!", self._publisher)

    def set_image(self, image) -> None:
        """
        :param image: Image to run the component on
        :type image:  numpy nd.array (height, width, channels)
        """
        if self._component_has_not_run_yet():
            self._image = image
        else:
            self._image = None  # to allow for garbage collection of first image


class YOEOBallDetectionComponent(IVisionComponent):
    """
    Component carries out the ball detection using YOEO
    """

    def __init__(self, node: Node):
        self._config: Dict = {}
        self._ball_detector: Union[None, yoeo_handler.YOEOBallDetector] = None
        self._debug_image: Union[None, debug.DebugImage] = None
        self._field_boundary_detector: Union[None, field_boundary.FieldBoundaryDetector] = None
        self._node: Node = node
        self._publisher: Union[None, rclpy.publisher.Publisher] = None

    def configure(self, config: Dict, yoeo: yoeo_handler.IYOEOHandler) -> None:
        self._ball_detector = yoeo_handler.YOEOBallDetector(yoeo)
        self._debug_image = DebugImageFactory.create(config)
        self._field_boundary_detector = YOEOFieldBoundaryDetectorFactory.create(config, yoeo)

        self._register_publisher(config)
        self._config = config

    def _register_publisher(self, new_config: Dict) -> None:
        self._publisher = ros_utils.create_or_update_publisher(
            self._node,
            self._config,
            new_config,
            self._publisher,
            'ROS_ball_msg_topic',
            BallArray
        )

    def run(self, image_msg) -> None:
        """
        :param image_msg: Image message
        :type image_msg:  sensor_msgs.msg._image.Image
        """
        candidates = self._get_best_ball_candidates()
        candidates_within_convex_fb = self._filter_for_best_candidates_within_convex_field_boundary(candidates)
        final_candidates = self._filter_by_candidate_threshold(candidates_within_convex_fb)

        ball_messages = self._create_ball_messages(final_candidates)
        balls_message = self._create_balls_message(image_msg, ball_messages)
        self._publish_balls_message(balls_message)

        self._add_candidates_to_debug_image(candidates)
        self._add_candidates_within_convex_fb_to_debug_image(candidates_within_convex_fb)
        self._add_final_candidates_to_debug_image(final_candidates)

    def _get_best_ball_candidates(self) -> List[candidate.Candidate]:
        return self._ball_detector.get_top_candidates(count=self._config['ball_candidate_max_count'])

    def _filter_for_best_candidates_within_convex_field_boundary(self, candidates: List[candidate.Candidate]) \
            -> List[candidate.Candidate]:
        return self._field_boundary_detector.candidates_under_convex_field_boundary(
            candidates,
            self._config['ball_candidate_field_boundary_y_offset']
        )

    def _filter_by_candidate_threshold(self, candidates: List[candidate.Candidate]) \
            -> List[candidate.Candidate]:
        return candidate.Candidate.rating_threshold(candidates, self._config['ball_candidate_rating_threshold'])

    @staticmethod
    def _create_ball_messages(candidates: List[candidate.Candidate]):
        """
        :rtype: List[soccer_vision_2d_msgs.msg._ball.Ball]  # TODO
        """
        return list(map(ros_utils.build_ball_msg, candidates))

    @staticmethod
    def _create_balls_message(image_msg, ball_messages):
        """
        :param image_msg: Image message
        :type image_msg:  sensor_msgs.msg._image.Image
        :param ball_messages: List of ball messages
        :type ball_messages: List[soccer_vision_2d_msgs.msg._ball.Ball]  # TODO
        :rtype: soccer_vision_2d_msgs.msg._ball_array.BallArray  # TODO
        """
        return ros_utils.build_ball_array_msg(image_msg.header, ball_messages)

    def _publish_balls_message(self, balls_message) -> None:
        """
        :param balls_message: Balls message
        :type balls_message: soccer_vision_2d_msgs.msg._ball_array.BallArray  # TODO
        """
        if balls_message:
            self._publisher.publish(balls_message)

    def _add_candidates_to_debug_image(self, candidates: List[candidate.Candidate]) -> None:
        self._debug_image.draw_ball_candidates(candidates, DebugImageColors.ball, thickness=1)

    def _add_candidates_within_convex_fb_to_debug_image(self, candidates: List[candidate.Candidate]) -> None:
        self._debug_image.draw_ball_candidates(candidates, DebugImageColors.ball, thickness=2)

    def _add_final_candidates_to_debug_image(self, candidates: List[candidate.Candidate]) -> None:
        self._debug_image.draw_ball_candidates(candidates, DebugImageColors.ball, thickness=3)

    def set_image(self, image) -> None:
        """
        :param image: Image to run the component on
        :type image:  numpy nd.array (height, width, channels)
        """
        self._field_boundary_detector.set_image(image)


class YOEOGoalpostDetectionComponent(IVisionComponent):
    """
    Component carries out the goalpost detection using YOEO
    """

    def __init__(self, node: Node):
        self._config: Dict = {}
        self._debug_image: Union[None, debug.DebugImage] = None
        self._field_boundary_detector: Union[None, field_boundary.FieldBoundaryDetector] = None
        self._goalpost_detector: Union[None, yoeo_handler.YOEOGoalpostDetector] = None
        self._node: Node = node
        self._publisher: Union[None, rclpy.publisher.Publisher] = None

    def configure(self, config: Dict, yoeo: yoeo_handler.IYOEOHandler) -> None:
        self._debug_image = DebugImageFactory.create(config)
        self._field_boundary_detector = YOEOFieldBoundaryDetectorFactory.create(config, yoeo)
        self._goalpost_detector = yoeo_handler.YOEOGoalpostDetector(yoeo)

        self._register_publisher(config)
        self._config = config

    def _register_publisher(self, new_config: Dict) -> None:
        self._publisher = ros_utils.create_or_update_publisher(
            self._node,
            self._config,
            new_config,
            self._publisher,
            'ROS_goal_posts_msg_topic',
            GoalpostArray
        )

    def run(self, image_msg) -> None:
        """
        :param image_msg: Image message
        :type image_msg:  sensor_msgs.msg._image.Image
        """
        candidates = self._get_candidates()
        final_candidates = self._get_candidates_within_convex_field_boundary(candidates)

        goalpost_messages = self._create_goalpost_messages(final_candidates)
        goalposts_message = self._create_goalposts_message(image_msg, goalpost_messages)
        self._publish_goalposts_message(goalposts_message)

        self._add_candidates_to_debug_image(self._goalpost_detector.get_candidates())
        self._add_final_candidates_to_debug_image(final_candidates)

    def _get_candidates(self) -> List[candidate.Candidate]:
        return self._goalpost_detector.get_candidates()

    def _get_candidates_within_convex_field_boundary(self, candidates: List[candidate.Candidate]) \
            -> List[candidate.Candidate]:
        return self._field_boundary_detector.candidates_under_convex_field_boundary(
            candidates,
            self._config['goal_post_field_boundary_y_offset']
        )

    @staticmethod
    def _create_goalpost_messages(candidates: List[candidate.Candidate]):
        """
        :rtype: List[soccer_vision_2d_msgs.msg._goalpost.Goalpost]  # TODO
        """
        return [ros_utils.build_goal_post_msg(candidate) for candidate in candidates]

    @staticmethod
    def _create_goalposts_message(image_msg, goalpost_messages):
        """
        :param image_msg: Image message
        :type image_msg:  sensor_msgs.msg._image.Image
        :param goalpost_messages: List of goalpost messages
        :type goalpost_messages: List[soccer_vision_2d_msgs.msg._goalpost.Goalpost]  # TODO
        :rtype: soccer_vision_2d_msgs.msg._goalpost_array.GoalpostArray  # TODO
        """
        return ros_utils.build_goal_post_array_msg(image_msg.header, goalpost_messages)

    def _publish_goalposts_message(self, goalposts_message) -> None:
        """
        :param goalposts_message: Goalposts message
        :type goalposts_message: soccer_vision_2d_msgs.msg._goalpost_array.GoalpostArray  # TODO
        """
        if goalposts_message:
            self._publisher.publish(goalposts_message)

    def _add_candidates_to_debug_image(self, candidates: List[candidate.Candidate]) -> None:
        self._debug_image.draw_obstacle_candidates(candidates, DebugImageColors.goalposts, thickness=1)

    def _add_final_candidates_to_debug_image(self, candidates: List[candidate.Candidate]) -> None:
        self._debug_image.draw_obstacle_candidates(candidates, DebugImageColors.goalposts, thickness=3)

    def set_image(self, image) -> None:
        """
        :param image: Image to run the component on
        :type image:  numpy nd.array (height, width, channels)
        """
        self._field_boundary_detector.set_image(image)


class YOEOFieldBoundaryDetectionComponent(IVisionComponent):
    """
    Component carries out the field boundary detection using YOEO
    """

    def __init__(self, node: Node):
        self._config: Dict = {}
        self._debug_image: Union[None, debug.DebugImage] = None
        self._field_boundary_detector: Union[None, field_boundary.FieldBoundaryDetector] = None
        self._node: Node = node
        self._publisher: Union[None, rclpy.publisher.Publisher] = None

    def configure(self, config: Dict, yoeo: yoeo_handler.IYOEOHandler) -> None:
        self._debug_image = DebugImageFactory.create(config)
        self._field_boundary_detector = YOEOFieldBoundaryDetectorFactory.create(config, yoeo)

        self._register_publisher(config)
        self._config = config

    def _register_publisher(self, new_config: Dict) -> None:
        self._publisher = ros_utils.create_or_update_publisher(
            self._node,
            self._config,
            new_config,
            self._publisher,
            'ROS_field_boundary_msg_topic',
            FieldBoundary
        )

    def run(self, image_msg) -> None:
        """
        :param image_msg: Image message
        :type image_msg:  sensor_msgs.msg._image.Image
        """
        convex_field_boundary_points = self._get_convex_field_boundary_points()
        field_boundary_msg = self._create_field_boundary_msg(image_msg, convex_field_boundary_points)
        self._publish_field_boundary_msg(field_boundary_msg)

        self._add_field_boundary_to_debug_image()
        self._add_convex_field_boundary_to_debug_image(convex_field_boundary_points)

    def _get_convex_field_boundary_points(self) -> List[Tuple[int, int]]:
        return self._field_boundary_detector.get_convex_field_boundary_points()

    @staticmethod
    def _create_field_boundary_msg(image_msg, field_boundary_points):
        """
        :param image_msg: Image message
        :type image_msg:  sensor_msgs.msg._image.Image
        :param field_boundary_points: Points of field boundary
        :type field_boundary_points: List[Tuple[int, int]]
        :rtype: soccer_vision_2d_msgs.msg._field_boundary.FieldBoundary  # TODO
        """
        return ros_utils.build_field_boundary_msg(image_msg.header, field_boundary_points)

    def _publish_field_boundary_msg(self, field_boundary_msg) -> None:
        """
        :param field_boundary_msg: Field boundary message
        :type field_boundary_msg: soccer_vision_2d_msgs.msg._field_boundary.FieldBoundary  # TODO
        """
        self._publisher.publish(field_boundary_msg)

    def _add_field_boundary_to_debug_image(self) -> None:
        field_boundary = self._field_boundary_detector.get_field_boundary_points()
        self._debug_image.draw_field_boundary(field_boundary, DebugImageColors.field_boundary)

    def _add_convex_field_boundary_to_debug_image(self, convex_field_boundary_points: List[Tuple[int, int]]) -> None:
        self._debug_image.draw_field_boundary(convex_field_boundary_points, DebugImageColors.field_boundary_convex)

    def set_image(self, image) -> None:
        """
        :param image: Image to run the component on
        :type image:  numpy nd.array (height, width, channels)
        """
        self._field_boundary_detector.set_image(image)


class YOEOLineDetectionComponent(IVisionComponent):
    """
    Component carries out the line detection using YOEO
    """

    def __init__(self, node: Node):
        self._config: Dict = {}
        self._debug_image: Union[None, debug.DebugImage] = None
        self._line_detector: Union[None, yoeo_handler.IYOEOSegmentation] = None
        self._node: Node = node
        self._publisher: Union[None, rclpy.publisher.Publisher] = None

    def configure(self, config: Dict, yoeo: yoeo_handler.IYOEOHandler) -> None:
        self._debug_image = DebugImageFactory.create(config)
        self._line_detector = yoeo_handler.YOEOLineSegmentation(yoeo)

        self._register_publisher(config)
        self._config = config

    def _register_publisher(self, new_config: Dict) -> None:
        self._publisher = ros_utils.create_or_update_publisher(
            self._node,
            self._config,
            new_config,
            self._publisher,
            'ROS_line_mask_msg_topic',
            Image
        )

    def run(self, image_msg) -> None:
        """
        :param image_msg: Image message
        :type image_msg:  sensor_msgs.msg._image.Image
        """
        line_mask = self._line_detector.get_mask_image()
        line_mask_msg = self._create_line_mask_msg(image_msg, line_mask)
        self._publish_line_mask_msg(line_mask_msg)

        self._add_line_mask_to_debug_image(line_mask)

    @staticmethod
    def _create_line_mask_msg(image_msg, line_mask):
        """
        :param image_msg: Image message
        :type image_msg:  sensor_msgs.msg._image.Image
        :param line_mask: Line mask
        :type line_mask: numpy ndarray (height, width, channels)
        :rtype: sensor_msgs.msg._image.Image
        """
        return ros_utils.build_image_msg(image_msg.header, line_mask, '8UC1')

    def _publish_line_mask_msg(self, line_mask_msg) -> None:
        """
        :param line_mask_msg: Line mask message
        :type line_mask_msg: sensor_msgs.msg._image.Image
        """
        self._publisher.publish(line_mask_msg)

    def _add_line_mask_to_debug_image(self, line_mask) -> None:
        """
        :param line_mask: Line mask
        :type line_mask: numpy ndarray (height, width, channels)
        """
        self._debug_image.draw_mask(line_mask, color=DebugImageColors.lines)

    def set_image(self, image) -> None:
        """
        :param image: Image to run the component on
        :type image:  numpy nd.array (height, width, channels)
        """
        pass  # Intentional


class YOEOFieldDetectionComponent(IVisionComponent):
    """
    Component carries out the field detection using YOEO
    """

    def __init__(self, node: Node):
        self._config: Dict = {}
        self._field_detector: Union[None, yoeo_handler.IYOEOSegmentation] = None
        self._node: Node = node
        self._publisher: Union[None, rclpy.publisher.Publisher] = None

    def configure(self, config: Dict, yoeo: yoeo_handler.IYOEOHandler) -> None:
        self._field_detector = yoeo_handler.YOEOFieldSegmentation(yoeo)
        self._log_status()

        self._register_publisher(config)
        self._config = config

    @staticmethod
    def _log_status():
        logger.info('Field mask WILL BE published')

    def _register_publisher(self, new_config: Dict) -> None:
        self._publisher = ros_utils.create_or_update_publisher(
            self._node,
            self._config,
            new_config,
            self._publisher,
            'ROS_field_mask_image_msg_topic',
            Image
        )

    def run(self, image_msg) -> None:
        """
        :param image_msg: Image message
        :type image_msg:  sensor_msgs.msg._image.Image
        """
        field_mask = self._get_field_mask()
        field_mask_msg = self._create_field_mask_msg(image_msg, field_mask)
        self._publish_field_mask_msg(field_mask_msg)

    def _get_field_mask(self):
        """
        :rtype: numpy ndarray (height, width, channels)
        """
        return self._field_detector.get_mask_image()

    @staticmethod
    def _create_field_mask_msg(image_msg, field_mask):
        """
        :param image_msg: Image message
        :type image_msg:  sensor_msgs.msg._image.Image
        :param field_mask: Field mask
        :type field_mask: numpy ndarray (height, width, channels)
        :rtype: sensor_msgs.msg._image.Image
        """
        return ros_utils.build_image_msg(image_msg.header, field_mask, '8UC1')

    def _publish_field_mask_msg(self, field_mask_msg) -> None:
        """
        :param field_mask_msg: Field mask message
        :type field_mask_msg: sensor_msgs.msg._image.Image
        """
        self._publisher.publish(field_mask_msg)

    def set_image(self, image) -> None:
        """
        :param image: Image to run the component on
        :type image:  numpy nd.array (height, width, channels)
        """
        pass  # Intentional


class YOEOObstacleDetectionComponent(IVisionComponent):
    """
    Component carries out the obstacle detection using YOEO
    """

    def __init__(self, node: Node):
        self._config: Dict = {}
        self._debug_image: Union[None, debug.DebugImage] = None

        self._misc_obstacles_detector: Union[None, obstacle.ColorObstacleDetector] = None
        self._opponents_detector: Union[None, obstacle.ColorObstacleDetector] = None
        self._team_mates_detector: Union[None, obstacle.ColorObstacleDetector] = None

        self._node: Node = node
        self._publisher: Union[None, rclpy.publisher.Publisher] = None

    def configure(self, config: Dict, yoeo: yoeo_handler.IYOEOHandler) -> None:
        own_color, opponent_color = self._determine_team_colors()
        self._team_mates_detector = YOEOObstacleDetectorFactory.create(
            config=config,
            yoeo=yoeo,
            color=own_color,
            subtractors=None
        )
        self._opponents_detector = YOEOObstacleDetectorFactory.create(
            config=config,
            yoeo=yoeo,
            color=opponent_color,
            subtractors=[self._team_mates_detector]
        )
        self._misc_obstacles_detector = YOEOObstacleDetectorFactory.create(
            config=config,
            yoeo=yoeo,
            color=None,
            subtractors=[self._team_mates_detector,
                         self._opponents_detector]
        )
        self._debug_image = DebugImageFactory.create(config)

        self._register_publisher(config)
        self._config = config

    @staticmethod
    def _determine_team_colors() -> Tuple[int, int]:
        own_color = ros_utils.get_robot_color_for_team(Robot().attributes.TEAM_OWN)
        opponent_color = ros_utils.get_robot_color_for_team(Robot().attributes.TEAM_OPPONENT)
        return own_color, opponent_color

    def _register_publisher(self, new_config: Dict) -> None:
        self._publisher = ros_utils.create_or_update_publisher(
            self._node,
            self._config,
            new_config,
            self._publisher,
            'ROS_obstacle_msg_topic',
            RobotArray
        )

    def run(self, image_msg) -> None:
        """
        :param image_msg: Image message
        :type image_msg:  sensor_msgs.msg._image.Image
        """
        list_of_obstacle_messages = []  # List[soccer_vision_2d_msgs.msg._robot.Robot]  # TODO
        self._add_team_mates_to(list_of_obstacle_messages)
        self._add_opponents_to(list_of_obstacle_messages)
        self._add_remaining_obstacles_to(list_of_obstacle_messages)

        obstacles_message = self._create_obstacles_message(image_msg, list_of_obstacle_messages)
        self._publish_obstacles_message(obstacles_message)

        self._add_obstacles_to_debug_image()

    def _add_team_mates_to(self, list_of_obstacle_messages) -> None:
        """
        :param list_of_obstacle_messages: List of obstacle messages
        :type list_of_obstacle_messages: List[soccer_vision_2d_msgs.msg._robot.Robot]  # TODO
        """
        team_mate_candidates = self._get_team_mate_candidates()
        team_mate_candidate_messages = self._create_obstacle_messages(Robot().attributes.TEAM_OWN, team_mate_candidates)
        list_of_obstacle_messages.extend(team_mate_candidate_messages)

    def _get_team_mate_candidates(self) -> List[candidate.Candidate]:
        return self._team_mates_detector.get_candidates()

    @staticmethod
    def _create_obstacle_messages(obstacle_type: Robot, candidates: List[candidate.Candidate]):
        """
        :rtype: List[soccer_vision_2d_msgs.msg._robot.Robot]  # TODO
        """
        return [ros_utils.build_robot_msg(obstacle_candidate, obstacle_type) for obstacle_candidate in candidates]

    def _add_opponents_to(self, list_of_obstacle_messages) -> None:
        """
        :param list_of_obstacle_messages: List of obstacle messages
        :type list_of_obstacle_messages: List[soccer_vision_2d_msgs.msg._robot.Robot]  # TODO
        """
        opponent_candidates = self._get_opponent_candidates()
        opponent_candidate_messages = self._create_obstacle_messages(Robot().attributes.TEAM_OPPONENT, opponent_candidates)
        list_of_obstacle_messages.extend(opponent_candidate_messages)

    def _get_opponent_candidates(self) -> List[candidate.Candidate]:
        return self._opponents_detector.get_candidates()

    def _add_remaining_obstacles_to(self, list_of_obstacle_messages: List) -> None:
        """
        :param list_of_obstacle_messages: List of obstacle messages
        :type list_of_obstacle_messages: List[soccer_vision_2d_msgs.msg._robot.Robot]  # TODO
        """
        remaining_candidates = self._get_remaining_candidates()
        remaining_candidate_messages = self._create_obstacle_messages(Robot().attributes.TEAM_UNKNOWN, remaining_candidates)
        list_of_obstacle_messages.extend(remaining_candidate_messages)

    def _get_remaining_candidates(self) -> List[candidate.Candidate]:
        return self._misc_obstacles_detector.get_candidates()

    @staticmethod
    def _create_obstacles_message(image_msg, obstacle_messages):
        """
        :param image_msg: Image message
        :type image_msg:  sensor_msgs.msg._image.Image
        :param obstacle_messages: Obstacle messages
        :type obstacle_messages: List[soccer_vision_2d_msgs.msg._robot.Robot]  # TODO
        :rtype: soccer_vision_2d_msgs.msg._robot_array.RobotArray  # TODO
        """
        return ros_utils.build_robot_array_msg(image_msg.header, obstacle_messages)

    def _publish_obstacles_message(self, obstacles_message) -> None:
        """
        :param obstacles_message: Obstacles message
        :type obstacles_message: soccer_vision_2d_msgs.msg._robot_array.RobotArray  # TODO
        """
        self._publisher.publish(obstacles_message)

    def _add_obstacles_to_debug_image(self) -> None:
        self._add_team_mates_to_debug_image()
        self._add_opponents_to_debug_image()
        self._add_remaining_objects_to_debug_image()

    def _add_team_mates_to_debug_image(self) -> None:
        team_mate_candidates = self._get_team_mate_candidates()
        self._debug_image.draw_obstacle_candidates(team_mate_candidates, DebugImageColors.team_mates, thickness=3)

    def _add_opponents_to_debug_image(self) -> None:
        opponent_candidates = self._get_opponent_candidates()
        self._debug_image.draw_obstacle_candidates(opponent_candidates, DebugImageColors.opponents, thickness=3)

    def _add_remaining_objects_to_debug_image(self) -> None:
        remaining_candidates = self._get_remaining_candidates()
        self._debug_image.draw_obstacle_candidates(remaining_candidates, DebugImageColors.misc_obstacles, thickness=3)

    def set_image(self, image) -> None:
        """
        :param image: Image to run the component on
        :type image:  numpy nd.array (height, width, channels)
        """
        self._team_mates_detector.set_image(image)
        self._opponents_detector.set_image(image)
        self._misc_obstacles_detector.set_image(image)


class DebugImageComponent(IVisionComponent):
    """
    Component published the debug image
    """

    def __init__(self, node):
        self._config: Dict = {}
        self._node: Node = node
        self._debug_image: Union[None, debug.DebugImage] = None
        self._publisher: Union[None, rclpy.publisher.Publisher] = None

    def configure(self, config: Dict, yoeo: yoeo_handler.IYOEOHandler) -> None:
        self._debug_image = DebugImageFactory.create(config)
        self._log_status()

        self._register_publisher(config)
        self._config = config

    @staticmethod
    def _log_status() -> None:
        logger.info('Debug images are published')

    def _register_publisher(self, config: Dict) -> None:
        self._publisher = ros_utils.create_or_update_publisher(
            self._node,
            self._config, config,
            self._publisher,
            'ROS_debug_image_msg_topic',
            Image
        )

    def run(self, image_msg) -> None:
        """
        :param image_msg: Image message
        :type image_msg:  sensor_msgs.msg._image.Image
        """
        debug_image_msg = self._create_debug_image_msg(image_msg)
        self._publish_debug_image_msg(debug_image_msg)

    def _create_debug_image_msg(self, image_msg):
        """
        :param image_msg: Image message
        :type image_msg:  sensor_msgs.msg._image.Image
        :rtype: sensor_msgs.msg._image.Image
        """
        return ros_utils.build_image_msg(image_msg.header, self._debug_image.get_image(), 'bgr8')

    def _publish_debug_image_msg(self, debug_image_msg) -> None:
        """
        :param debug_image_msg: Debug image message
        :type debug_image_msg: sensor_msgs.msg._image.Image
        """
        self._publisher.publish(debug_image_msg)

    def set_image(self, image) -> None:
        """
        :param image: Image to run the component on
        :type image:  numpy nd.array (height, width, channels)
        """
        self._debug_image.set_image(image)
