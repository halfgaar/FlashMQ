#!/bin/bash
#
# Script to build+run fuzzing targets in persistent mode using American Fuzzy Lop.

thisfile=$(readlink --canonicalize "$0")
thisdir=$(dirname "$thisfile")
projectdir=$(dirname "$thisdir")

die() {
	>&2 echo "Fatal error: $*"
	exit 9
}

if [[ -z "$AFL_ROOT" ]]; then
  afl_fallback_from_path=$(command -v afl-fuzz)
  if [[ -z "$afl_fallback_from_path" ]]
  then
    echo "ERROR: alf-fuzz not found. Please set the AFL_ROOT environment variable"
    exit 1
  else
    AFL_ROOT=$(dirname "$afl_fallback_from_path")
    echo "WARNING: You didn't set AFL_ROOT but I found afl-fuzz in $AFL_ROOT, I hope the other tools are also there"
  fi
fi

# AFL_ROOT is our own variable name, newer AFL++ warn that they don't know what
# it means. Solution: unexport the variable
export -n AFL_ROOT

set -u

usage() {
  >&2 echo "Usage: [ build | run | build_and_run | attach | clean ]"
}
die() {
  >&2 echo "A fatal error occurred: $*"
  exit 9
}

TMUXSESSION=flashmqfuzzpersistent
COMMAND="build_and_run"
if [[ $# -ge 1 ]]; then
  COMMAND="$1"
fi
SPECIFIC_BINARY=""
if [[ $# -ge 2 ]]; then
  SPECIFIC_BINARY="$2"
fi

do_build() {
  # We currently need LTO/fast because they understand the macros we use
  export CC="$AFL_ROOT/afl-clang-fast"
  CC_CLANG_LTO="$AFL_ROOT/afl-clang-lto"
  [[ -e "$CC_CLANG_LTO" ]] && export CC="$CC_CLANG_LTO"

  export CXX="$AFL_ROOT/afl-clang-fast++"
  CXX_CLANG_LTO="$AFL_ROOT/afl-clang-lto++"
  [[ -e "$CXX_CLANG_LTO" ]] && export CXX="$CXX_CLANG_LTO"

  echo "Using for \$CC:  $CC"
  echo "Using for \$CXX: $CXX"

  mkdir -p "${thisdir}/build"
  cd "${thisdir}/build" || die "Something peculiar went wrong."

  cmake -DCMAKE_BUILD_TYPE="fuzz-persistent" "$projectdir" || die "CMake failed."
  if [[ -z "$SPECIFIC_BINARY" ]]
  then
    make -j "fuzzing_targets" || die "make failed."
  else
    make -j "fuzz_${SPECIFIC_BINARY}" || die "make failed."
  fi
}

do_run() {
  # list-sessions has a -f option but it doesn't seem to work for me
  if tmux list-sessions | grep "^$TMUXSESSION:" > /dev/null
  then
    >&2 echo "Fuzzing is already going on in the background. Use 'attach' to attach."
    exit 1
  fi


  declare -a FUZZING_TARGETS
  HUMAN_LIST_OF_TARGETS=$'\n'
  for i in "${thisdir}/build/fuzz_"*;
  do
    if [[ -x "$i" ]];
    then
      FUZZING_TARGETS+=("$(basename "$i")")
      HUMAN_LIST_OF_TARGETS="$HUMAN_LIST_OF_TARGETS- $(basename "$i")"$'\n'
    fi
  done

  INPUT="${projectdir}/fuzztests"

  if [[ -z "$SPECIFIC_BINARY" ]]
  then
    echo "AFL output will be stored in ${thisdir}/output"
    echo "Fuzzing targets: ${HUMAN_LIST_OF_TARGETS}"

    tmux new-session -s "$TMUXSESSION" -d "echo 'Fuzzing targets: ${HUMAN_LIST_OF_TARGETS}Cycle through the windows to see the fuzzing going on'; read"

    for BINARY in "${FUZZING_TARGETS[@]}"
    do
      OUTPUT="${thisdir}/output/${BINARY}"
      mkdir -p "$OUTPUT"
      tmux new-window -t "$TMUXSESSION" -n "$BINARY" "'$AFL_ROOT/afl-fuzz' -i '${INPUT}' -o '${OUTPUT}' -T '$BINARY' -- '${thisdir}/build/$BINARY'; echo 'Press [enter] to exit'; read"
    done
    tmux next-window -t "$TMUXSESSION"  # cycle through to the first window with the README
    tmux attach-session -t "$TMUXSESSION"
  else
    OUTPUT="${thisdir}/output/${SPECIFIC_BINARY}"
    BINARY="fuzz_$SPECIFIC_BINARY"

    echo "AFL output will be stored in ${OUTPUT}"
    echo "Fuzzing target: ${SPECIFIC_BINARY}"

    echo mkdir -p "$OUTPUT"
    tmux new-session -s "$TMUXSESSION" -n "$BINARY" "'$AFL_ROOT/afl-fuzz' -i '${INPUT}' -o '${OUTPUT}' -T '$BINARY' -- '${thisdir}/build/$BINARY'; echo 'Press [enter] to exit'; read"
  fi
}

do_clean() {
  rm -rf "$thisdir/build"
}

do_attach() {
  tmux attach-session -t "$TMUXSESSION"
}

if [[ "$COMMAND" == "build" ]]
then
  >&2 echo "Building..."
  do_build
elif [[ "$COMMAND" == "run" ]]
then
  >&2 echo "Running..."
  do_run
elif [[ "$COMMAND" == "build_and_run" ]]
then
  >&2 echo "Building and running..."
  do_build
  echo ""
  echo ""
  do_run
elif [[ "$COMMAND" == "clean" ]]
then
  do_clean
elif [[ "$COMMAND" == "attach" ]]
then
  do_attach
else
  >&2 echo "Unknown option $COMMAND"
  usage
  exit 1
fi
