#!/bin/sh
# SPDX-License-Identifier: GPL-2.0

exec tools/bazel run --config=tangorpro --config=fast //private/devices/google/tangorpro:tangorpro_dist "$@"

