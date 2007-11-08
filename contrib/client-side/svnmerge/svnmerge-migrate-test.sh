#!/bin/bash
#
# A test harness for validation of svnmerge-migrate-history.py.

SUBVERSION_PREFIX='/usr/local/subversion'
export PATH="$SUBVERSION_PREFIX/bin:$PATH"
export PYTHONPATH="$SUBVERSION_PREFIX/lib/svn-python"

INITIAL_DIR="`pwd`"
TMP_DIR='/tmp'
REPOS_URL="file://$TMP_DIR/repos"
SCRIPT_DIR=$(python -c "import os; print os.path.abspath(\"`dirname $0`\")")

cd $TMP_DIR
rm -rf repos wc

echo 'Creating repository, and populating it with baseline data...'
svnadmin create repos
svn co $REPOS_URL wc
mkdir wc/trunk wc/branches wc/tags
echo 'hello world' > wc/trunk/hello-world.txt
svn add wc/*
svn ci -m 'Populate repos with skeletal data.' wc

echo 'Creating a branch, and initializing merge tracking data...'
svn cp wc/trunk wc/branches/B
svn ci -m 'Create branch B.' wc
cd wc/branches/B
$HOME/src/subversion/contrib/client-side/svnmerge/svnmerge.py init
svn ci -m 'Initialize svnmerge.py merge tracking info on branch B.'
#svn merge --record-only -r4:7 $REPOS_URL/trunk  ### Not working (?)
svn ps svn:mergeinfo '/trunk:4-7' .
svn ci -m 'Mix in Subversion 1.5 merge tracking info on branch B.'
cd -

# Run the migration script, passing on any arguments.
$SCRIPT_DIR/svnmerge-migrate-history.py "$@" || exit 1

# Report the results.
echo 'New WC mergeinfo:'
svn up wc
svn pl -vR wc | grep '/trunk:1,4-7' && echo 'PASS' || \
  (echo 'FAIL: Unexpected mergeinfo:' && svn pl -vR wc) >&2 && exit 1
