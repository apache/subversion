#!/bin/bash

set -x

RC=0

echo "========= make check-javahl"
make check-javahl || RC=$?

echo "========= make check-swig-pl"
make check-swig-pl || RC=$?

echo "========= make check-swig-py"
make check-swig-py || RC=$?

echo "========= make check-swig-rb"
make check-swig-rb || RC=$?

exit ${RC}
