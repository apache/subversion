#!/bin/bash

# more exercise than test...

SVN=subversion/svn/svn
MUCC=contrib/client-side/mucc
SL=subversion/svnlook/svnlook
#REPO=file://`pwd`/repo
REPO=svn://localhost/repo
#REPO=http://localhost:8888/obj/repo

rm -rf repo
#svnadmin create --bdb-txn-nosync --fs-type bdb repo
svnadmin create --fs-type fsfs repo

echo [users] >> repo/conf/passwd
echo aa = bb >> repo/conf/passwd
echo [general] >> repo/conf/svnserve.conf
echo passwd-db = passwd >> repo/conf/svnserve.conf
echo anon-access = write >> repo/conf/svnserve.conf

$SVN mkdir -m "a directory" $REPO/foo
$SVN import repo/README.txt -m "a file" $REPO/z.c
$SL tree --full-paths repo /

function stat()
{
  $SVN log -vrhead $REPO
  $SL tree --full-paths repo /
}

$MUCC cp 2 $REPO/z.c $REPO/foo/z.c \
      cp 2 $REPO/foo $REPO/foo/bar
stat

$MUCC cp 3 $REPO/foo $REPO/zig \
      rm   $REPO/zig/bar \
      mv   $REPO/foo $REPO/zig/zag
stat

$MUCC mv   $REPO/z.c $REPO/zig/zag/bar/y.c \
      cp 2 $REPO/z.c $REPO/zig/zag/bar/x.c
stat

$MUCC mv      $REPO/zig/zag/bar/y.c $REPO/zig/zag/bar/y%20y.c \
      cp head $REPO/zig/zag/bar/y.c $REPO/zig/zag/bar/y%2520y.c 
stat

$MUCC mv      $REPO/zig/zag/bar/y%20y.c   $REPO/zig/zag/bar/z\ z1.c \
      cp head $REPO/zig/zag/bar/y%2520y.c $REPO/zig/zag/bar/z%2520z.c \
      cp head $REPO/zig/zag/bar/y\ y.c    $REPO/zig/zag/bar/z\ z2.c
stat

$MUCC mv   $REPO/zig/zag $REPO/zig/foo \
      rm   $REPO/zig/foo/bar/z\ z1.c \
      rm   $REPO/zig/foo/bar/z%20z2.c \
      rm   $REPO/zig/foo/bar/z%2520z.c \
      cp 5 $REPO/zig/zag/bar/x.c $REPO/zig/foo/bar/z%20z1.c
stat

$MUCC rm   $REPO/zig/foo/bar \
      cp 8 $REPO/zig/z.c $REPO/zig/foo/bar
stat

$MUCC rm   $REPO/zig/foo/bar \
      cp 8 $REPO/zig/foo/bar $REPO/zig/foo/bar \
      rm   $REPO/zig/foo/bar/z%20z1.c
stat

$MUCC rm      $REPO/zig/foo \
      cp head $REPO/zig/foo/bar $REPO/zig/foo
stat

$MUCC rm   $REPO/zig \
      cp 3 $REPO/foo $REPO/foo \
      cp 3 $REPO/foo $REPO/foo/foo \
      cp 3 $REPO/foo $REPO/foo/foo/foo \
      rm   $REPO/foo/foo/bar \
      rm   $REPO/foo/foo/foo/bar \
      cp 3 $REPO/foo $REPO/foo/foo/foo/bar
stat
