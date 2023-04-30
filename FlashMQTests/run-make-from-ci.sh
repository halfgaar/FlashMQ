#!/bin/bash

set -u

mkdir buildtests
cd buildtests || exit 1
qmake -spec "$1" ../FlashMQTestsMeta.pro

nprocs=4
if _nprocs=$(nproc); then
  nprocs="$_nprocs"
fi

make -j "$nprocs"
