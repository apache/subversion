#!/bin/sh
#### run-fs-tests.sh --- run filesystem test programs

## Remove database files created by the tests.
if [ -d test-repo-1 ]; then
  rm -fr test-repo-*;
fi

for test_pgm in key-test skel-test strings-reps-test fs-test; do
  echo "  - running all sub-tests in $test_pgm ..."
  ./$test_pgm
done
