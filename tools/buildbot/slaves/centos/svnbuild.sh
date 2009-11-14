#!/bin/bash

set -x

export MAKEFLAGS=-j4

echo "========= autogen.sh"
./autogen.sh || exit $?

echo "========= configure"
#            --with-junit=/usr/share/java/junit.jar
#            --with-jdk=/usr/lib/jvm/java-1.6.0-openjdk-1.6.0.0.x86_64 \
./configure --enable-javahl --enable-maintainer-mode \
            --with-neon=/usr \
            --with-apxs=/usr/sbin/apxs \
            --without-berkeley-db \
            --with-apr=/usr \
            --with-jdk \
            --with-apr-util=/usr \
            || exit $?

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
