#!/bin/sh

export PATH=/usr/local/subversion/bin:$PATH

cd /tmp
rm -rf repos wc

echo 'Creating repository, and populating it with baseline data...'
svnadmin create repos
svn co file:///tmp/repos wc
mkdir wc/trunk wc/branches wc/tags
echo 'hello world' > wc/trunk/hello-world.txt
svn add wc/*
svn ci -m 'Populate repos with skeletal data.' wc

echo 'Creating a branch, and initializing merge tracking data...'
svn cp wc/trunk wc/branches/B
svn ci -m 'Create branch B.' wc
cd wc/branches/B
$HOME/src/subversion/contrib/client-side/svnmerge/svnmerge.py init
svn ci -m 'Initialize merge tracking info.'
cd -
