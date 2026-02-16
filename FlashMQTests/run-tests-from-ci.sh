#!/bin/bash

STDERR_LOG=$(mktemp)

cd buildtests || exit 1

# Using --abort-on-first-fail because the output can be hard to find in CI when it's swamped out by the rest.
if ./flashmq-tests --abort-on-first-fail 2> "$STDERR_LOG" ; then
  echo -e '\033[01;32mSUCCESS!\033[00m'
else
  echo -e '\033[01;31mBummer\033[00m'
  echo -e "\n\nTail of stderr:\n\n"
  tail -n 200 "$STDERR_LOG"
  exit 1
fi
