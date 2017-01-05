## *********************************************************
## 
## File autogenerated for the bitbots_motion package 
## by the dynamic_reconfigure package.
## Please do not edit.
## 
## ********************************************************/

from dynamic_reconfigure.encoding import extract_params

inf = float('inf')

config_description = {'upper': 'DEFAULT', 'lower': 'groups', 'srcline': 233, 'name': 'Default', 'parent': 0, 'srcfile': '/opt/ros/kinetic/lib/python2.7/dist-packages/dynamic_reconfigure/parameter_generator.py', 'cstate': 'true', 'parentname': 'Default', 'class': 'DEFAULT', 'field': 'default', 'state': True, 'parentclass': '', 'groups': [], 'parameters': [{'srcline': 259, 'description': 'False: Servos will be turned of when falling; true: robot tries to fall dynamically', 'max': True, 'cconsttype': 'const bool', 'ctype': 'bool', 'srcfile': '/opt/ros/kinetic/lib/python2.7/dist-packages/dynamic_reconfigure/parameter_generator.py', 'name': 'dyn_falling_active', 'edit_method': '', 'default': False, 'level': 1, 'min': False, 'type': 'bool'}, {'srcline': 259, 'description': 'Hardness of the floor, 1.25 for artificial turf(soft), 1 for hard ground', 'max': 2.0, 'cconsttype': 'const double', 'ctype': 'double', 'srcfile': '/opt/ros/kinetic/lib/python2.7/dist-packages/dynamic_reconfigure/parameter_generator.py', 'name': 'ground_coefficient', 'edit_method': '', 'default': 1.25, 'level': 1, 'min': 0.0, 'type': 'double'}, {'srcline': 259, 'description': 'Threshold harder -> earlier reaction, but more false positives.', 'max': 0.0, 'cconsttype': 'const double', 'ctype': 'double', 'srcfile': '/opt/ros/kinetic/lib/python2.7/dist-packages/dynamic_reconfigure/parameter_generator.py', 'name': 'threshold_gyro_y_front', 'edit_method': '', 'default': -50.0, 'level': 1, 'min': -100.0, 'type': 'double'}, {'srcline': 259, 'description': 'Threshold harder -> earlier reaction, but more false positives.', 'max': -100.0, 'cconsttype': 'const double', 'ctype': 'double', 'srcfile': '/opt/ros/kinetic/lib/python2.7/dist-packages/dynamic_reconfigure/parameter_generator.py', 'name': 'threshold_gyro_y_back', 'edit_method': '', 'default': 50.0, 'level': 1, 'min': 0.0, 'type': 'double'}, {'srcline': 259, 'description': 'Threshold harder -> earlier reaction, but more false positives.', 'max': 0.0, 'cconsttype': 'const double', 'ctype': 'double', 'srcfile': '/opt/ros/kinetic/lib/python2.7/dist-packages/dynamic_reconfigure/parameter_generator.py', 'name': 'threshold_gyro_x_right', 'edit_method': '', 'default': -30.0, 'level': 1, 'min': -100.0, 'type': 'double'}, {'srcline': 259, 'description': 'Threshold harder -> earlier reaction, but more false positives.', 'max': -100.0, 'cconsttype': 'const double', 'ctype': 'double', 'srcfile': '/opt/ros/kinetic/lib/python2.7/dist-packages/dynamic_reconfigure/parameter_generator.py', 'name': 'threshold_gyro_x_left', 'edit_method': '', 'default': 30.0, 'level': 1, 'min': 0.0, 'type': 'double'}], 'type': '', 'id': 0}

min = {}
max = {}
defaults = {}
level = {}
type = {}
all_level = 0

#def extract_params(config):
#    params = []
#    params.extend(config['parameters'])    
#    for group in config['groups']:
#        params.extend(extract_params(group))
#    return params

for param in extract_params(config_description):
    min[param['name']] = param['min']
    max[param['name']] = param['max']
    defaults[param['name']] = param['default']
    level[param['name']] = param['level']
    type[param['name']] = param['type']
    all_level = all_level | param['level']

