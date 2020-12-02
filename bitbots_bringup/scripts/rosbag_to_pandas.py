#!/usr/bin/env python3

import rosbag
import argparse
import rosmsg
import pandas as pd

"""
This script reads in a rosbag and ouputs a pandas dataframe representration of the data. This is usefull for later 
processing, e.g. in scikit-learn or for creating plots.
"""


class COLORS:
    HEADER = '\033[95m'
    OKBLUE = '\033[94m'
    OKGREEN = '\033[92m'
    WARNING = '\033[93m'
    FAIL = '\033[91m'
    ENDC = '\033[0m'
    BOLD = '\033[1m'
    UNDERLINE = '\033[4m'


parser = argparse.ArgumentParser()
parser.add_argument("bag")

args = parser.parse_args()

bag = rosbag.Bag(args.bag)
topics = []
types = []
for key, value in bag.get_type_and_topic_info().topics.items():
    topics.append(key)
    types.append(value[0])

col_width = max(len(word) for row in [topics, types] for word in row) + 4  #
for i in range(len(topics)):
    print(f"{COLORS.BOLD}{COLORS.OKBLUE}[{i}]:{COLORS.ENDC} {topics[i].ljust(col_width)}{types[i]}")

topic_selections_str = input("Select data source. Use a format like 0 2 3.\n")

topic_selections_str_list = topic_selections_str.split()
topic_selections = []

for topic_selection_str in topic_selections_str_list:
    try:
        topic_selections.append(int(topic_selection_str))
    except ValueError as ex:
        print(ex)
        exit(-1)

selected_topics_list = [topics[i] for i in topic_selections]

data_selections = []
for topic_selection in topic_selections:
    print(f"{COLORS.BOLD}{COLORS.OKGREEN}Available Fields from {COLORS.OKBLUE}{topics[topic_selection]}{COLORS.ENDC}")
    print(rosmsg.get_msg_text(types[topic_selection]))
    single_topic_data_selection = input("Specify which fields you want seperated with spaces. Header stamp and seq "
                                        "are included automatically.\n")
    data_selections.append(single_topic_data_selection.split())


def recursive_getattr(obj, field_list):
    if len(field_list) == 1:
        return getattr(obj, field_list[0])
    else:
        return recursive_getattr(getattr(obj, field_list[0]), field_list[1:])


frames = []
for data_selection in data_selections:
    frames.append(pd.DataFrame(columns=["header"].extend(data_selection)))

msg_generator = bag.read_messages(topics=[topics[i] for i in topic_selections])

seperate_input = input("Do you want to extract lists and tuples into separate columns? [y|n]")
seperate = seperate_input == "y" or seperate_input == "Y"
if seperate:
    print("Will separate\n")
else:
    print("Will not separate\n")

remove_remove_time_offset_input = input("Do you want the time to start at 0 (remove time offset)? [y|n]")
remove_time_offset = remove_remove_time_offset_input == "y" or remove_remove_time_offset_input == "Y"
if seperate:
    print("Will remove offset\n")
else:
    print("Will not remove offset\n")

print(f"{COLORS.BOLD}{COLORS.WARNING}Extracting data{COLORS.ENDC}")
header_warning_printed = False
current_msg_index = 0
first_stamp = [None] * len(selected_topics_list)
for msg in msg_generator:
    # give some status feedback since this can take a while
    if current_msg_index % 1000 == 0:
        print(f"{current_msg_index}")
    current_msg_index += 1

    try:
        i = selected_topics_list.index(msg.topic)
    except ValueError:
        continue

    fields = {}
    try:
        time_stamp = msg.message.header.stamp.to_sec()
    except AttributeError:
        if not header_warning_printed:
            header_warning_printed = True
            print(
                f"{COLORS.WARNING}No header found in one of the messages. Will use receiving time as stamp.{COLORS.ENDC}")
        time_stamp = msg.timestamp.to_sec()
    if not first_stamp[i]:
        # remember first timestamp to remove offset
        first_stamp[i] = time_stamp
    # remove offset if wanted
    if remove_time_offset:
        fields["stamp"] = time_stamp - first_stamp[i]
    else:
        fields["stamp"] = time_stamp

    for data_selection in data_selections[i]:
        f = recursive_getattr(msg.message, data_selection.split("."))
        # extract lists in own rows
        if seperate and (isinstance(f, list) or isinstance(f, tuple)):
            j = 0
            for entry in f:
                fields[f"{data_selection}_{j}"] = entry
                j += 1
        else:
            fields[data_selection] = f
    frames[i] = frames[i].append(fields, ignore_index=True)

for i, frame in enumerate(frames):
    print(f"{COLORS.OKBLUE}{selected_topics_list[i]}{COLORS.ENDC}")
    print(frame.info())
    frame.to_pickle(selected_topics_list[i][1:].replace("/", "-") + ".pickle")
