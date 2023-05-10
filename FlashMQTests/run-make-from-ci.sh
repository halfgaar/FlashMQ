#!/bin/bash

spec="linux-g++"
extra_config=""

while [ -n "$*" ]; do
  flag=$1
  value=$2

  case "$flag" in
    "--spec")
      spec="$value"
      shift
    ;;
    "--extra-config")
      extra_config="$value"
      shift
    ;;
    "--")
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

mkdir buildtests || exit 1
cd buildtests || exit 1
qmake -spec "$spec" "$extra_config" ../FlashMQTestsMeta.pro

nprocs=4
if _nprocs=$(nproc); then
  nprocs="$_nprocs"
fi

make -j "$nprocs"
