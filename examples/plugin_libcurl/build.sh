#!/bin/bash

thisfile=$(readlink --canonicalize "$0")
thisdir=$(dirname "$thisfile")

BUILD_TYPE="Release"
if [[ "$1" == "Debug" ]]; then
  BUILD_TYPE="Debug"
fi

BUILD_DIR="build-plugin-libcurl-$BUILD_TYPE"

set -eu

if [[ -e "$BUILD_DIR" ]]; then
  >&2 echo "$BUILD_DIR already exists. Considering fatal error because you should run 'make' in it if you want to keep using it."
  exit 1
else
  mkdir "$BUILD_DIR"
fi

cd "$BUILD_DIR"

cmake -DCMAKE_BUILD_TYPE="$BUILD_TYPE" "$thisdir"
make -j 4
