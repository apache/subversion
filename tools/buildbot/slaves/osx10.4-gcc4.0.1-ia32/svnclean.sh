#!/bin/bash

# ../unmount_ramd.sh

echo "========= make extraclean"
test -e Makefile && (make extraclean || exit $?)
rm -rf ../build/*
rm -rf .svn
rm -rf .buildbot-sourcedata

exit 0
