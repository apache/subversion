#!/bin/bash

set -x

echo "========= unmount RAM disc"
# ignore the result: if there was no ramdisc, that's fine
test -e ../unmount-ramdrive && ../unmount-ramdrive

echo "========= make extraclean"
test -e Makefile && (make extraclean || exit $?)

exit 0
