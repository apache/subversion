#!/bin/sh

# Make sure checkout-test binary exists

if [ ! -x ./checkout-test ]; then
    echo "Error:  can't find checkout-test executable"
    exit 1
fi

# First get rid of any remnant of the last run.
rm -rf this

./checkout-test checkout-1.delta this/is/a/test/dir
