#!/bin/sh

OBJ_DIR=`pwd`
cd ..
INST_DIR=`pwd`/inst/apr-0.9
SRC_DIR=`pwd`/apr-0.9

cd ${SRC_DIR}
./buildconf
./configure "--prefix=${INST_DIR}" "$@"
