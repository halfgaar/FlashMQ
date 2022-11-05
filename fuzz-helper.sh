#!/bin/bash
#
# Quick 'n dirty Script to build and run FlashMQ with American Fuzzy Lop.

thisfile=$(readlink --canonicalize "$0")
thisdir=$(dirname "$thisfile")

if [[ -z "$AFL_ROOT" ]]; then
  echo "ERROR: set AFL_ROOT environment variable"
  exit 1
fi

if [[ -z "$FLASHMQ_SRC" ]]; then
  echo "ERROR: set FLASHMQ_SRC environment variable"
  exit 1
fi

set -u

if [[ ! -d "$FLASHMQ_SRC/fuzztests" ]]; then
  echo "Folder 'fuzztests' not found in '$FLASHMQ_SRC'"
  exit 1
fi

if [[ "$1" == "build" ]]; then

  export CC="$AFL_ROOT/afl-gcc"
  export CXX="$AFL_ROOT/afl-g++"

  mkdir "fuzzbuild"
  cd "fuzzbuild" || exit 1

  "$thisdir/build.sh" Debug

  if [[ -f "./FlashMQBuildDebug/flashmq" ]]; then
    cp -v "./FlashMQBuildDebug/flashmq" ..
  fi
fi

if [[ "$1" == "run" ]]; then
  INPUTDIR="$FLASHMQ_SRC/fuzztests"
  OUTPUTDIR="fuzzoutput"
  BINARY="./flashmq"

  if [[ ! -d "$OUTPUTDIR" ]]; then
    mkdir "$OUTPUTDIR"
  fi

  tmux new-session -s flashmqfuzz -d  "'$AFL_ROOT/afl-fuzz' -m 200 -M primary     -i '$INPUTDIR' -o '$OUTPUTDIR' '$BINARY' --fuzz-file '@@'; sleep 5"
  tmux split-window -t flashmqfuzz -v "'$AFL_ROOT/afl-fuzz' -m 200 -S secondary01 -i '$INPUTDIR' -o '$OUTPUTDIR' '$BINARY' --fuzz-file '@@'; sleep 5"
  tmux split-window -t flashmqfuzz -h "'$AFL_ROOT/afl-fuzz' -m 200 -S secondary02 -i '$INPUTDIR' -o '$OUTPUTDIR' '$BINARY' --fuzz-file '@@'; sleep 5"
  tmux select-pane -t flashmqfuzz -U
  tmux split-window -t flashmqfuzz -h "'$AFL_ROOT/afl-fuzz' -m 200 -S secondary03 -i '$INPUTDIR' -o '$OUTPUTDIR' '$BINARY' --fuzz-file '@@'; sleep 5"

  tmux attach-session -d -t flashmqfuzz
fi
