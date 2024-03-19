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

usage() {
  >&2 echo "Usage: [ build | run ]"
}

if [[ ! -d "$FLASHMQ_SRC/fuzztests" ]]; then
  echo "Folder 'fuzztests' not found in '$FLASHMQ_SRC'"
  exit 1
fi

if [[ $# -ne 1 ]]; then
  usage
  exit 1
fi

if [[ "$1" == "build" ]]; then

  # AFLplusplus has some fancy versions:
  # *-lto (collision free instrumentation at link time) is preferred
  # *-fast is also better than base (but less so than LTO)
  echo "Detecting afl-gcc / alf-gcc-fast / alf-clang-lto ..."
  export CC="$AFL_ROOT/afl-gcc"
  CC_GNU_FAST="${CC}-fast"
  CC_CLANG_LTO="$AFL_ROOT/afl-clang-lto"
  [[ -e "$CC_GNU_FAST" ]] && export CC="$CC_GNU_FAST"
  [[ -e "$CC_CLANG_LTO" ]] && export CC="$CC_CLANG_LTO"

  echo "Detecting afl-g++ / afl-g++-fast / alf-clang-lto++ ..."
  export CXX="$AFL_ROOT/afl-g++"
  CXX_GNU_FAST="${CXX}-fast"
  CXX_CLANG_LTO="$AFL_ROOT/afl-clang-lto++"
  [[ -e "$CXX_GNU_FAST" ]] && export CXX="$CXX_GNU_FAST"
  [[ -e "$CXX_CLANG_LTO" ]] && export CXX="$CXX_CLANG_LTO"

  echo "Using for \$CC:  $CC"
  echo "Using for \$CXX: $CXX"

  mkdir "fuzzbuild"
  cd "fuzzbuild" || exit 1

  "$thisdir/build.sh" Debug

  if [[ -f "./FlashMQBuildDebug/flashmq" ]]; then
    cp -v "./FlashMQBuildDebug/flashmq" ..
  fi

elif [[ "$1" == "run" ]]; then
  RUNNERS=("primary" "secondary01" "secondary02" "secondary03")

  INPUTDIR="$FLASHMQ_SRC/fuzztests"
  OUTPUTDIR="fuzzoutput"
  RUNDIR="${thisdir}/fuzzrun"

  for runner in "${RUNNERS[@]}"
  do
    runner_dir="$RUNDIR/$runner"
    mkdir -p "$runner_dir/storage"

    # Overriding the system wide config from /etc/flashmq/flashmq.conf
    # not specifying log_file:    logs will go to stdout instead
    # not specifying storage_dir: turn off persistent storage
    echo "quiet       yes" > "$runner_dir/flashmq.conf"
  done

  BINARY="./flashmq"

  if [[ ! -d "$OUTPUTDIR" ]]; then
    mkdir "$OUTPUTDIR"
  fi

  tmux new-session -s flashmqfuzz -d  "'$AFL_ROOT/afl-fuzz' -m 200 -M primary     -i '$INPUTDIR' -o '$OUTPUTDIR' '$BINARY' --config-file ${RUNDIR}/primary/flashmq.conf --fuzz-file '@@'; sleep 5"
  tmux split-window -t flashmqfuzz -v "'$AFL_ROOT/afl-fuzz' -m 200 -S secondary01 -i '$INPUTDIR' -o '$OUTPUTDIR' '$BINARY' --config-file ${RUNDIR}/secondary01/flashmq.conf --fuzz-file '@@'; sleep 5"
  tmux split-window -t flashmqfuzz -h "'$AFL_ROOT/afl-fuzz' -m 200 -S secondary02 -i '$INPUTDIR' -o '$OUTPUTDIR' '$BINARY' --config-file ${RUNDIR}/secondary02/flashmq.conf --fuzz-file '@@'; sleep 5"
  tmux select-pane -t flashmqfuzz -U
  tmux split-window -t flashmqfuzz -h "'$AFL_ROOT/afl-fuzz' -m 200 -S secondary03 -i '$INPUTDIR' -o '$OUTPUTDIR' '$BINARY' --config-file ${RUNDIR}/secondary03/flashmq.conf --fuzz-file '@@'; sleep 5"

  tmux attach-session -d -t flashmqfuzz

else
  >&2 echo "Unknown option $1."
  usage
  exit 1
fi
