#!/bin/bash

set -x

echo "========= autogen.sh"
./autogen.sh || exit $?

echo "========= configure"
./configure --disable-static --enable-shared \
            --enable-maintainer-mode \
            --with-neon=/usr/local/neon-0.25.5 \
            --with-apxs=/usr/sbin/apxs \
            --without-berkeley-db \
            --with-apr=/usr/local/apr \
            --with-apr-util=/usr/local/apr || exit $?

echo "========= make"
make || exit $?

# echo "========= make swig-py"
# make swig-py || exit $?

# echo "========= make swig-pl"
# make swig-pl || exit $?

#echo "========= make swig-rb"
#make swig-rb || exit $?

exit 0
