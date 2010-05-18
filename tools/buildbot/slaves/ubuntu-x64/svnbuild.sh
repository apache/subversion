#!/bin/bash

set -x

export MAKEFLAGS=-j4

echo "========= autogen.sh"
./autogen.sh || exit $?

echo "========= configure"
./configure --enable-javahl --enable-maintainer-mode \
            --with-neon=/usr/local \
            --with-apxs=/home/hwright/dev/svn-buildbot/usr/bin/apxs \
            --without-berkeley-db \
            --with-apr=/usr/local/apr \
            --with-jdk=/usr/lib/jvm/java-6-sun-1.6.0.10 \
            --with-junit=/usr/share/java/junit.jar \
            --with-apr-util=/usr/local/apr \
            --with-sqlite=/usr/local || exit $?

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
