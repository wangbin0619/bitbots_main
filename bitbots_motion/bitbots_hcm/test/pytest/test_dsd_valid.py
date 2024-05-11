#!/usr/bin/env python3
import glob
import os

from bitbots_hcm import hcm_dsd
from dynamic_stack_decider import DSD


def test_dsd_valid():
    # Create empty blackboard
    dummy_blackboard = object
    # Create DSD
    dsd = DSD(dummy_blackboard())

    # Register actions and decisions
    dsd.register_actions(hcm_dsd.actions.__path__[0])
    dsd.register_decisions(hcm_dsd.decisions.__path__[0])

    # Load all dsd files to check if they are valid\
    for dsd_file in glob.glob(os.path.join(hcm_dsd.__path__[0], "*.dsd")):
        dsd.load_behavior(dsd_file)
