#!/bin/sh

if test -d "z"; then
    :
else
    mkdir z
    mkdir z/A
    mkdir z/A/B
    mkdir z/A/C
    mkdir z/D
    mkdir z/D/E
    mkdir z/D/F
    mkdir z/G
    mkdir z/G/H
    mkdir z/G/I
    touch z/A/file
fi

echo "Testing normal usage"
./target-test z/A/B z/A z/A/C z/D/E z/D/F z/D z/G z/G/H z/G/I
echo

echo "Testing with identical arguments (that are dirs)"
./target-test z/A z/A z/A z/A
echo

echo "Testing with identical arguments (that are files)"
./target-test z/A/file z/A/file z/A/file z/A/file
echo

echo "Testing with a single dir"
./target-test z/A
echo

echo "Testing with a single file"
./target-test z/A/file
echo
