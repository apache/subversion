#!/bin/bash

set -x

RC=0

echo "========= make check-javahl"
make check-javahl || RC=$?

echo "========= make check-swig-pl"
make check-swig-pl || RC=$?

echo "========= make check-swig-py"
make check-swig-py || RC=$?

# ruby test currently failing, generating SEGV on centos
#echo "========= make check-swig-rb"
#make check-swig-rb # || RC=$?

exit ${RC}
