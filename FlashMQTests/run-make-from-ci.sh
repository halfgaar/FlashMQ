#!/bin/bash

extra_configs=()
compiler="g++"

while [ -n "$*" ]; do
  flag=$1
  value=$2

  case "$flag" in
    "--compiler")
      compiler="$value"
      shift
    ;;
    "--extra-config")
      extra_configs+=("$value")
      shift
    ;;
    "--")
      shift
      break
    ;;
    *)
      echo -e "unknown option $flag\n"
      exit 1
    ;;
  esac

  shift
done

set -u

for item in "${extra_configs[@]}"; do
  extra_configs_expanded=("${extra_configs_expanded[@]}" "-D" "$item")
done

CXX="$compiler" cmake -S . -B buildtests "${extra_configs_expanded[@]}"

nprocs=4
if _nprocs=$(nproc); then
  nprocs="$_nprocs"
fi

make -C buildtests -j "$nprocs"
