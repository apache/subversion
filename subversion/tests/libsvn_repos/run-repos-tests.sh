#!/bin/sh
#### run-fs-tests.sh --- run filesystem test programs

## Remove database files cretaed by the tests.
if [ -d test-repo-1 ]; then
  rm -fr test-repo-*;
fi

> ./tests.log
for test_pgm in repos-test; do
  echo;
  echo -n "Running all sub-tests in ${test_pgm}...";
  ./${test_pgm} >> tests.log;
  if [ $? -eq 0 ];
  then
    echo "SUCCESS";
  else
    echo;
    echo "at least one sub-test FAILED, check tests.log:"; echo; \
    cat tests.log | grep FAIL; \
  fi
done
