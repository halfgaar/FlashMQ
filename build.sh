#!/bin/bash

thisfile=$(readlink --canonicalize "$0")
thisdir=$(dirname "$thisfile")

BUILD_TYPE="Release"
if [[ "$1" == "Debug" ]]; then
  BUILD_TYPE="Debug"
fi

BUILD_DIR="FlashMQBuild$BUILD_TYPE"

if [[ -e "$BUILD_DIR" ]]; then
  echo "$BUILD_DIR already exists. Not doing anything. You can run 'make' in it, if you want."
  exit 1
fi

set -e
set -u

mkdir "$BUILD_DIR"
cd "$BUILD_DIR"

cmake -DCMAKE_BUILD_TYPE="$BUILD_TYPE" "$thisdir"
make
