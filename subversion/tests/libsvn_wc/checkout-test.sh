#!/bin/sh

# Make sure checkout-test binary exists

if [ ! -x ./checkout-test ]; then
    echo "Error:  can't find checkout-test executable"
    exit 1
fi

# Run the binary

./checkout-test < checkout-1.delta > checkout-1.out

# Copied from hashdump-test.sh:
#
## # Compare the two output files
## 
## diff checkout.out checkout2.out
## 
## if [ $? -ne 0 ]; then
##     echo "Error: checkout output files aren't identical."
##     exit 1
## else
##     exit 0
## fi
