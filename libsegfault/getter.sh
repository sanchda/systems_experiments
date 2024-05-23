#!/bin/bash
set -euo pipefail

# Lookup for correct arch
declare -A osname
osname["x86_64"]="amd64"
osname["aarch64"]="arm64"

# Check to make sure dpkg-deb is installed
if ! [ -x "$(command -v dpkg-deb)" ]; then
  echo 'Error: dpkg-deb is not installed.' >&2
  exit 1
fi

# Before we download anything, check that wget or curl is installed
if ! [ -x "$(command -v wget)" ] && ! [ -x "$(command -v curl)" ]; then
  echo 'Error: wget or curl is not installed.' >&2
  exit 1
fi

# Get the path to the resource
base_url="https://ftp.debian.org/debian/pool/main/g/glibc"
filename_template="libc6_2.31-13+deb11u5_ARCHITECTURE_STRING.deb"
arch=${osname[$(uname -m)]}
filename=${filename_template/ARCHITECTURE_STRING/$arch}
url=${base_url}/${filename}

# Make a temporary directory to download the deb file
temp_dir="/tmp/libsegfault_deb"
mkdir -p $temp_dir

# check that the dir exists
if [ ! -d $temp_dir ]; then
  echo 'Error: ${tmp_dir} could not be made.' >&2
  exit 1
fi


# We should be good to go, download and extract the deb file
pushd $temp_dir > /dev/null
if command -v curl > /dev/null; then
  curl -fsSLO $url
elif command -v wget > /dev/null; then
  wget $url
else
  echo "Error: curl or wget not found"
  exit 1
fi

# Extract the deb file
mkdir -p extracted
dpkg-deb -x ${filename} extracted

# Get the path to libSegFault.so
so_path=$(realpath $(find extracted -name "libSegFault.so"))
echo $so_path
popd > /dev/null
