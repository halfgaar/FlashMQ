#!/bin/bash

set -u

nprocs=4
if _nprocs=$(nproc); then
  nprocs="$_nprocs"
fi

make -j "$nprocs"
