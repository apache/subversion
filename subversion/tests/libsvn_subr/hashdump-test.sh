#!/bin/sh

# Make sure hashdump-test binary exists

if [ ! -x ./hashdump-test ]; then
    echo "Error:  can't find hashdump-test executable"
    exit 1
fi

# Run the binary

./hashdump-test

# Compare the two output files

diff hashdump.out hashdump2.out

if [ $? -ne 0 ]; then
    echo "Error: hashdump output files aren't identical."
    exit 1
else
    exit 0
fi


