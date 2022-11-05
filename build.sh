#!/bin/bash

thisfile=$(readlink --canonicalize "$0")
thisdir=$(dirname "$thisfile")

BUILD_TYPE="Release"
if [[ "$1" == "Debug" ]]; then
  BUILD_TYPE="Debug"
fi

BUILD_DIR="FlashMQBuild$BUILD_TYPE"

set -eu

if [[ -e "$BUILD_DIR" ]]; then
  >&2 echo "$BUILD_DIR already exists. You can run 'make' in it, if you want.
"
else
  mkdir "$BUILD_DIR"
fi

cd "$BUILD_DIR"

cmake -DCMAKE_BUILD_TYPE="$BUILD_TYPE" "$thisdir"
make -j
cpack

FLASHMQ_VERSION=$(./flashmq --version | grep -Ei 'Flashmq.*version.*' | grep -Eo '[0-9]+\.[0-9]+\.[0-9]+[^ ]*')

if command -v linuxdeploy-x86_64.AppImage &> /dev/null; then
  linuxdeploy-x86_64.AppImage --create-desktop-file --icon-file "../flashmq.png" --appdir "AppImageDir" --executable "flashmq" --output appimage
  mv flashmq-*.AppImage "flashmq-${FLASHMQ_VERSION}-linux-amd64.AppImage"
fi
