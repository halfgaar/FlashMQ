#!/bin/bash

thisfile=$(readlink --canonicalize "$0")
thisdir=$(dirname "$thisfile")
script_name=$(basename "$0")

DEFAULT_BUILD_TYPE="Release"

usage() {
  cat <<EOF
Usage:
  $script_name [--fail-on-doc-failure] [--njobs-override <nr>] <build_type>
  $script_name --help     Display this help

  <build_type>  'Debug' or 'Release'; default: $DEFAULT_BUILD_TYPE
EOF
}

FAIL_ON_DOC_FAILURE=""
BUILD_TYPE="$DEFAULT_BUILD_TYPE"
NJOBS_OVERRIDE=""

while [[ "$#" -gt 0 ]]; do
  case "$1" in
    --help|-h)
      usage
      exit 0
      ;;
    --fail-on-doc-failure)
      FAIL_ON_DOC_FAILURE="yeah"
      shift
      ;;
    --njobs-override)
      shift
      NJOBS_OVERRIDE="$1"
      shift
      ;;
    --*|-*)
      echo -e "\e[31mUnknown option: \e[1m$1\e[0m" >&2
      usage >&2
      exit 2
      ;;
    Debug|Release)
      if [[ "$#" -gt 1 ]]; then
        echo -e "\e[31mRelease type (\e[1m$1\e[22m) must be the last argument\e[0m" >&2
        usage >&2
        exit 2
      fi
      BUILD_TYPE="$1"
      shift
      ;;
  esac
done

if ! [[ "$NJOBS_OVERRIDE" =~ ^[0-9]*$ ]] ; then
  >&2 echo "--njobs-override must be a number"
  exit 1
fi

if ! make -C "$thisdir/man"; then
  if [[ -z "$FAIL_ON_DOC_FAILURE" ]]; then
    echo -e "\e[33mIgnoring failed man page builds; run \e[1m$script_name\e[22m with the \e[1m--fail-on-doc-failure\e[22m option to make such failures fatal.\e[0m"
  else
    echo -e "\e[31mMaking the man pages failed; dying now in obedience of the \e[1m--fail-on-doc-failure\e[22m option.\e[0m"
    exit 3
  fi
fi

BUILD_DIR="FlashMQBuild$BUILD_TYPE"

set -eu

if [[ -e "$BUILD_DIR" ]]; then
  >&2 echo "$BUILD_DIR already exists. You can run 'make' in it, if you want.
"
else
  mkdir "$BUILD_DIR"
fi

nprocs=4

if _nprocs=$(nproc); then
  nprocs="$_nprocs"
fi

if [[ -n "$NJOBS_OVERRIDE" ]]; then
  nprocs="$NJOBS_OVERRIDE"
fi

cd "$BUILD_DIR"

cmake -DCMAKE_BUILD_TYPE="$BUILD_TYPE" "$thisdir"
make -j "$nprocs"
cpack

FLASHMQ_VERSION=$(./flashmq --version | grep -Ei 'Flashmq.*version.*' | grep -Eo '[0-9]+\.[0-9]+\.[0-9]+[^ ]*')

if command -v linuxdeploy-x86_64.AppImage &> /dev/null; then
  linuxdeploy-x86_64.AppImage --create-desktop-file --icon-file "../flashmq.png" --appdir "AppImageDir" --executable "flashmq" --output appimage
  mv flashmq-*.AppImage "flashmq-${FLASHMQ_VERSION}-linux-amd64.AppImage"
fi
