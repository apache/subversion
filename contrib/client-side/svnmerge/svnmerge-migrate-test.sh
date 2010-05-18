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

do_test()
{
    expected_mergeinfo=$1
    flags=$2

    if [ "x${flags}x" = "xx" ]; then
        echo "******* TEST BEGIN (no flags) ********"
    else
        echo "******* TEST BEGIN ${flags} ********"
    fi
    cd $TMP_DIR
    rm -rf repos wc
    echo 'Creating repository, and populating it with baseline data...'
    svnadmin create repos
    svn -q co $REPOS_URL wc
    
    # r1
    mkdir wc/trunk wc/branches wc/tags
    echo 'hello world' > wc/trunk/hello-world.txt
    svn -q add wc/*
    svn -q ci -m 'Populate repos with skeletal data.' wc

    # r2
    echo 'Creating a branch, and initializing merge tracking data...'
    svn cp -q wc/trunk wc/branches/B
    svn ci -q -m 'Create branch B.' wc

    # r3
    (cd wc/branches/B \
        && $SCRIPT_DIR/svnmerge.py init > /dev/null \
        && svn ci -q -m 'Initialize svnmerge.py merge tracking info on branch B.')

    # r4 - r7
    echo 'Making some trunk commits...'
    echo `date` >> wc/trunk/hello-world.txt \
        && svn -q ci -m 'Tweak hello-world.txt' wc
    echo `date` >> wc/trunk/hello-world.txt \
        && svn -q ci -m 'Tweak hello-world.txt' wc
    echo `date` >> wc/trunk/hello-world.txt \
        && svn -q ci -m 'Tweak hello-world.txt' wc
    echo `date` >> wc/trunk/hello-world.txt \
        && svn -q ci -m 'Tweak hello-world.txt' wc

    # r8
    echo 'Using svnmerge to merge some trunk commits to branch B...'
    svn up -q wc
    (cd wc/branches/B \
        && $SCRIPT_DIR/svnmerge.py merge --revision 4-6 > /dev/null \
        && svn -q ci -m 'svnmerge merge some trunk revisions.')

    # r9
    echo 'Using svn merge --record-only to fake some merges branch B...'
    (cd wc/branches/B \
        && svn merge --record-only -c7 $REPOS_URL/trunk \
        && svn -q ci -m 'svn merge --record-only a trunk revision.')
    svn up -q wc
    
    # Migrate the mergeinfo!
    cd - > /dev/null  
    $SCRIPT_DIR/svnmerge-migrate-history.py $TMP_DIR/repos -v /branches ${flags}
    if [ "$?" != "0" ]; then
        echo '******* TEST FAIL: Mergeinfo migration failed *******' >&2
        return 1
    fi

    # Now see if we got the right results.
    svn pl -vR $REPOS_URL | grep ${expected_mergeinfo} > /dev/null
    if [ "$?" != "0" ]; then
        (echo '******* TEST FAIL: Unexpected mergeinfo:' \
            && svn pget -R svn:mergeinfo $REPOS_URL \
            && echo "Expected (regex): ${expected_mergeinfo} *******") >&2
        return 1
    else
        echo '******* TEST PASS *******'
    fi
}

do_test '/trunk:4-7' ''
do_test '/trunk:1,4-7' '--naive-mode'
