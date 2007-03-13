#!/bin/bash

# more exercise than test...

SVN=subversion/svn/svn
MUCC=contrib/client-side/mucc
SL=subversion/svnlook/svnlook
REPO=file://`pwd`/repo
#REPO=svn://localhost/repo
#REPO=http://localhost:8888/obj/repo

rm -rf repo
#svnadmin create --bdb-txn-nosync --fs-type bdb repo
svnadmin create --fs-type fsfs repo

echo [users] >> repo/conf/passwd
echo aa = bb >> repo/conf/passwd
echo [general] >> repo/conf/svnserve.conf
echo passwd-db = passwd >> repo/conf/svnserve.conf
echo anon-access = write >> repo/conf/svnserve.conf

function stat()
{
  $SVN log -vqrhead $REPO
  $SL tree --full-paths repo /
}


$MUCC -U $REPO \
      mkdir foo
stat

$MUCC -U $REPO \
      put /dev/null z.c
stat

$MUCC -U $REPO \
      cp 2 z.c foo/z.c \
      cp 2 foo foo/bar
stat

$MUCC -U $REPO \
      cp 3 foo zig \
      rm   zig/bar \
      mv   foo zig/zag
stat

$MUCC -U $REPO \
      mv   z.c zig/zag/bar/y.c \
      cp 2 z.c zig/zag/bar/x.c
stat

$MUCC -U $REPO \
      mv      zig/zag/bar/y.c zig/zag/bar/y%20y.c \
      cp head zig/zag/bar/y.c zig/zag/bar/y%2520y.c 
stat

$MUCC -U $REPO \
      mv      zig/zag/bar/y%20y.c   zig/zag/bar/z\ z1.c \
      cp head zig/zag/bar/y%2520y.c zig/zag/bar/z%2520z.c \
      cp head zig/zag/bar/y\ y.c    zig/zag/bar/z\ z2.c
stat

$MUCC -U $REPO \
      mv   zig/zag zig/foo \
      rm   zig/foo/bar/z\ z1.c \
      rm   zig/foo/bar/z%20z2.c \
      rm   zig/foo/bar/z%2520z.c \
      cp 5 zig/zag/bar/x.c zig/foo/bar/z%20z1.c
stat

$MUCC -U $REPO \
      rm   zig/foo/bar \
      cp 8 zig/z.c zig/foo/bar
stat

$MUCC -U $REPO \
      rm   zig/foo/bar \
      cp 8 zig/foo/bar zig/foo/bar \
      rm   zig/foo/bar/z%20z1.c
stat

$MUCC -U $REPO \
      rm      zig/foo \
      cp head zig/foo/bar zig/foo
stat

$MUCC -U $REPO \
      rm   zig \
      cp 3 foo foo \
      cp 3 foo foo/foo \
      cp 3 foo foo/foo/foo \
      rm   foo/foo/bar \
      rm   foo/foo/foo/bar \
      cp 3 foo foo/foo/foo/bar
stat
