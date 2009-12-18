#!/bin/bash
BASEURL=$1
VERSION=$2
wget -nc $BASEURL/{{md5,sha1}sums,svn_version.h.dist,subversion{-deps,}-$VERSION.{{zip,tar.bz2}{.asc,},tar.gz.asc}}
bzip2 -dk subversion{-deps,}-$VERSION.tar.bz2
gzip -9n subversion{-deps,}-$VERSION.tar
md5sum -c md5sums
sha1sum -c sha1sums
