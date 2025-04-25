#!/bin/bash
if [ -z "$1" ]; then
  echo "Usage: $0 <libname>"
  exit 1
fi
lib=$1
export libs=$(python -m pip show $lib | awk '/Location/ {print $2}')/$lib

# This is a list of visited libraries
visited=()

add_visited() {
  # Do another realpath just to be extra sure
  visited+=(`realpath $1`)
}

# If the given path is a file with a TLS section, print the symbols in the TLS section
check_tls() {
  # If we've seen this before, return
  lib=`realpath $1`
  if [[ " ${visited[@]} " =~ " ${lib} " ]]; then
    return
  fi

  # Let's add this to the list of visited libraries
  add_visited ${lib}
  if readelf -lW ${lib} | grep -q TLS; then
    echo "TLS section found in ${lib}"
    echo "  size:"  `readelf -lW ${lib} | grep TLS | awk '{print $6}'`
    # List the symbols in the TLS section
    readelf -sW ${lib} | grep TLS
  fi

  # Finally, check the dependencies of this library
  local deps=$(ldd ${lib} | grep -oP '/[^ ]*')
  for dep in $deps; do
    if [ -f $dep ]; then
        check_tls $dep
    fi
  done
}

for f in `find $libs -name '*.so*'`; do
  check_tls $f
done
