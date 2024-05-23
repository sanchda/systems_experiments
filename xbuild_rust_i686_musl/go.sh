#!/bin/bash
docker buildx create --use --name rust_builder
docker buildx build \
  --progress plain \
  --target final \
  --output type=docker \
  -t rust-nightly-alpine:i686 \
  . && \
docker run --rm -it \
  -v ./configs:/configs \
  rust-nightly-alpine:i686
