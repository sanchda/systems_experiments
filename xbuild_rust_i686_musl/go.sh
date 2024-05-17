#!/bin/bash
# Of course, linux/386 is actually i686 support.  Duh.
# This might help: docker run --rm --privileged multiarch/qemu-user-static --reset -p yes
docker buildx create --use --name larger_log --driver-opt env.BUILDKIT_STEP_LOG_MAX_SIZE=50000000
docker buildx build --progress plain --output type=docker -t rust-nightly-alpine:i686 . && \
  docker run --rm -it -v ./configs:/configs rust-nightly-alpine:i686
