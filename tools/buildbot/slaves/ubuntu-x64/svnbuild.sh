#!/bin/bash

set -x

export MAKEFLAGS=-j4

echo "========= autogen.sh"
./autogen.sh || exit $?

echo "========= configure"
./configure --enable-javahl --enable-maintainer-mode \
            --without-berkeley-db \
            --with-jdk=/usr/lib/jvm/java-6-openjdk/ \
            --with-junit=/usr/share/java/junit.jar || exit $?

echo "========= make"
make || exit $?

echo "========= make javahl"
make javahl -j1 || exit $?

echo "========= make swig-py"
make swig-py || exit $?

echo "========= make swig-pl"
make swig-pl -j1 || exit $?

echo "========= make swig-rb"
make swig-rb -j1 || exit $?

exit 0
