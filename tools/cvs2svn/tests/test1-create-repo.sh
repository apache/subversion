#!/bin/sh -e

# Create a tiny CVS repository in ./test1/repo.

rm -rf test1

CVSROOT="`pwd`/test1/repo" export CVSROOT

mkdir -p test1
cd test1
cvs -Q init

# Create and import some files.

mkdir -p import
(
    cd import
    echo "This is file1.txt." > file1.txt
    echo "This is deadfile." > deadfile
    cvs -Q import -m 'Initial import.' main vendor-tag release-tag
)

mkdir -p work1
(
    cd work1
    cvs -Q checkout main
    cd main
    {
	echo begin 666 file2.bin
	echo '-THIS IS FILE2.BIN..'
	echo ' '
	echo end
    } | uudecode
    rm -f deadfile
    cvs -Q add -kb file2.bin
    cvs -Q remove deadfile
    cvs -Q commit -m 'Log message one.'
)
