#!/bin/sh
#### run-fs-tests.sh --- run filesystem test programs

## Remove database files cretaed by the tests.
if [ -d test-repo-1 ]; then
  rm -fr test-repo-*;
fi

for test_pgm in repos-test; do
  echo "  - running all sub-tests in $test_pgm ..."
  ./$test_pgm
done
