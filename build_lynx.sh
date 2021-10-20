#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
BUILD_KERNEL=1 \
DEVICE_KERNEL_BUILD_CONFIG=private/devices/google/lynx/build.config.lynx \
private/gs-google/build_slider.sh "$@"
