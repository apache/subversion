#!/bin/sh

if test "$#" != 1; then
  echo "ERROR ($0): not enough arguments. Must supply SOURCE-DIR."
  exit 1
fi

# Make sure checkout-test binary exists

TEST_DELTA=$1/subversion/tests/xml/co1-inline.xml

if [ ! -x ./checkout-test ]; then
    echo "Error:  can't find checkout-test executable"
    exit 1
fi

# First get rid of any remnants of the last run.
rm -rf this a
for num in 1 2; do
   rm -rf ${num}/this
done

# Check out all the deltas.
# for num in 1 2; do
#    echo ./checkout-test checkout-${num}.delta ${num}/this/is/a/test/dir
#    ./checkout-test checkout-${num}.delta ${num}/this/is/a/test/dir
# done

./checkout-test ${TEST_DELTA} this

# That's it.  Right now, we test by inspecting them by hand.
