#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

exec tools/bazel run --config=felix --config=fast --config=stamp //private/devices/google/felix:felix_dist "$@"

