#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

exec tools/bazel run --config=felix --config=fast //private/devices/google/felix:felix_dist "$@"

