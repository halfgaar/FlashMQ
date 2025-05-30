#!/bin/bash

STDERR_LOG=$(mktemp)


export FMQ_RUN_ID=$EPOCHSECONDS  # this is only granular to the second but that should be fine

cd buildtests || exit 1

if gdb --batch -x ../fmq-generate-core-file.py -x ../save-core-on-sigvtalrm.gdb --args ./flashmq-tests 2> "$STDERR_LOG" ; then
  echo -e '\033[01;32mSUCCESS!\033[00m'
else
  echo -e '\033[01;31mBummer\033[00m'
  echo -e "\n\nTail of stderr:\n\n"
  tail -n 200 "$STDERR_LOG"

  for i in "core.${FMQ_RUN_ID}."*
  do
	  [[ "$i" == "core.${FMQ_RUN_ID}.*" ]] && break
	  echo ""
	  echo ",#############################################,"
    echo "| $i |"
    echo "'#############################################'"
    gdb --batch --ex "bt -full" "./flashmq-tests" "$i"
  done

  exit 1
fi
