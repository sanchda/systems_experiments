# Note: presumes the current BUILDPLATFORM has rust on Debian-based system
FROM --platform=$BUILDPLATFORM rust:latest as builder

# Install required packages and clean up to reduce layer size
RUN apt-get update && apt-get install -y \
    autoconf \
    bash \
    bison \
    build-essential \
    clang \
    cmake \
    curl \
    file \
    flex \
    g++ \
    gawk \
    gcc \
    git \
    gperf \
    help2man \
    libc6-dev-i386 \
    libexpat1-dev \
    libffi-dev \
    libncurses5-dev \
    libssl-dev \
    libtool \
    libtool-bin \
    llvm \
    make \
    musl-dev \
    musl-tools \
    ninja-build \
    perl \
    pkg-config \
    python3 \
    python3-dev \
    python3-pip \
    texinfo \
    tzdata \
    wget && \
    apt-get clean && rm -rf /var/lib/apt/lists/*

# Download and install crosstool-ng
ENV CT_NG_VERSION=1.26.0
RUN cd /usr/local/src && \
    wget http://crosstool-ng.org/download/crosstool-ng/crosstool-ng-${CT_NG_VERSION}.tar.xz && \
    tar -xf crosstool-ng-${CT_NG_VERSION}.tar.xz && \
    mv crosstool-ng-${CT_NG_VERSION} crosstool-ng && \
    cd crosstool-ng && \
    ./bootstrap && \
    ./configure --prefix=/usr/local && \
    make && \
    make install && \
    rm -rf /usr/local/src/crosstool-ng

# Create a cross-compilation toolchain for x86_64 gnu to i686 musl
RUN mkdir -p /usr/local/src/ct-ng
COPY configs/i686-unknown-linux-musl /usr/local/src/ct-ng/.config
WORKDIR /usr/local/src/ct-ng
RUN ct-ng build && rm -rf /usr/local/src/ct-ng

# Download the Rust nightly source code
WORKDIR /
ENV RUST_VERSION=nightly
RUN wget https://static.rust-lang.org/dist/rustc-nightly-src.tar.xz && \
    tar -xf rustc-nightly-src.tar.xz && \
    mv rustc-nightly-src rust && \
    rm rustc-nightly-src.tar.xz

# Try to build the target Rust toolchain
WORKDIR /rust
COPY config.toml .
RUN python3 ./x.py build --stage 1 --host x86_64-unknown-linux-gnu --target i686-unknown-linux-musl && \
    python3 ./x.py dist --host x86_64-unknown-linux-gnu --target i686-unknown-linux-musl

## Final stage: Copy the built Rust toolchain to a minimal Alpine image
FROM --platform=$TARGETPLATFORM alpine:latest as final
RUN mkdir -p /rust/build
COPY --from=builder /rust/build/x86_64-unknown-linux-gnu/stage2-std/i686-unknown-linux-musl /rust/build/i686-unknown-linux-musl
