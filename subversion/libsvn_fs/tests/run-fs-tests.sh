#!/bin/sh
#### run-fs-tests.sh --- run filesystem test programs

> ./tests.log
for test_pgm in skel-test; do
  if ./${test_pgm} >> ./tests.log; then :; else
    echo "Test program \`${test_pgm}' exited with status $?" >&2
    exit 1
  fi
done

if grep '^FAIL:' ./tests.log; then
  exit 1
else
  exit 0
fi
