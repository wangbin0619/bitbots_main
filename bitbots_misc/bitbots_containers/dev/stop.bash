#!/usr/bin/env bash

IMAGE_NAME=bitbots-dev-iron
CONTAINER_NAME=`docker ps --filter status=running --filter ancestor=$IMAGE_NAME --format "{{.Names}}"`

if [[ -z $CONTAINER_NAME ]]; then
    echo "The container is not running"
else
    docker stop $CONTAINER_NAME > /dev/null  # Do not print container name
fi
