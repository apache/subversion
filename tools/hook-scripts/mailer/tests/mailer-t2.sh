#!/bin/sh
#
# USAGE: ./mailer-t2.sh
#
# Uses some interesting revisions for testing:
#   r1849322 -- copied from; removals
#   r1884610 -- copied unchanged; file removed
#   r1902575 -- add with props
#   r1912632 -- many files modified
#

SVNRDUMP="/usr/bin/svnrdump"
SVNADMIN="/usr/bin/svnadmin"
SVNDUMPFILTER="/usr/bin/svndumpfilter"

ROOT_URL="https://svn.apache.org/repos/asf/subversion/trunk"

THIS_DIR=$(dirname "$0")
THIS_DIR=$(cd "$THIS_DIR" && pwd)

MAILER="$THIS_DIR"/../mailer.py
CONFIG="$THIS_DIR"/../mailer.conf.example

T2_ROOT="$THIS_DIR"/t2-root
if (mkdir "$T2_ROOT" 2> /dev/null) || /bin/false; then
    echo "Created: $T2_ROOT"
else
    echo "Exists: $T2_ROOT"
fi
cd "$T2_ROOT"

REPOS_DIR="$T2_ROOT"/repos

REV1=1849322 ; DUMP1=dump.${REV1}.gz
REV2=1884610 ; DUMP2=dump.${REV2}.gz
REV3=1902575 ; DUMP3=dump.${REV3}.gz
REV4=1912632 ; DUMP4=dump.${REV4}.gz

if test ! -f $DUMP1 ; then
    # We need some historical revisions for the copies in $REV1.
    # Just get content from the subdir with the bulk of changes,
    # and then filter out empty revisions.
    RPREV=1849295
    echo Fetching r$RPREV:r$REV1 ...
    $SVNRDUMP dump -r $RPREV:$REV1 $ROOT_URL/subversion/bindings \
        | $SVNDUMPFILTER include --drop-empty-revs / \
        | gzip --best \
        > $DUMP1
fi
if test ! -f $DUMP2 ; then
    echo Fetching r$REV2 ...
    RPREV=$(($REV2 - 1))
    $SVNRDUMP dump -r $RPREV:$REV2 $ROOT_URL | gzip --best > $DUMP2
fi
if test ! -f $DUMP3 ; then
    echo Fetching r$REV3 ...
    RPREV=$(($REV3 - 1))
    $SVNRDUMP dump -r $RPREV:$REV3 $ROOT_URL | gzip --best > $DUMP3
fi
if test ! -f $DUMP4 ; then
    echo Fetching r$REV4 ...
    RPREV=$(($REV4 - 1))
    $SVNRDUMP dump -r $RPREV:$REV4 $ROOT_URL | gzip --best > $DUMP4
fi

if test ! -d ./repos.$REV1 ; then
    echo Creating repos for $REV1 ...
    $SVNADMIN create ./repos.$REV1
    gunzip --to-stdout $DUMP1 | $SVNADMIN load --quiet ./repos.$REV1
fi
# The revision of interest in this repository: r28
$MAILER commit ./repos.$REV1 28 $CONFIG > output.$REV1
if cmp --quiet output.$REV1 ../t2-reference/output.$REV1; then
    echo SUCCESS for $REV1
else
    echo FAIL -- differences detected for $REV1
fi

if test ! -d ./repos.$REV2 ; then
    echo Creating repos for $REV2 ...
    $SVNADMIN create ./repos.$REV2
    gunzip --to-stdout $DUMP2 | $SVNADMIN load --quiet ./repos.$REV2
fi
# The revision of interest in this repository: r2
$MAILER commit ./repos.$REV2 2 $CONFIG > output.$REV2
if cmp --quiet output.$REV2 ../t2-reference/output.$REV2; then
    echo SUCCESS for $REV2
else
    echo FAIL -- differences detected for $REV2
fi

if test ! -d ./repos.$REV3 ; then
    echo Creating repos for $REV3 ...
    $SVNADMIN create ./repos.$REV3
    gunzip --to-stdout $DUMP3 | $SVNADMIN load --quiet ./repos.$REV3
fi
# The revision of interest in this repository: r2
$MAILER commit ./repos.$REV3 2 $CONFIG > output.$REV3
if cmp --quiet output.$REV3 ../t2-reference/output.$REV3; then
    echo SUCCESS for $REV3
else
    echo FAIL -- differences detected for $REV3
fi

if test ! -d ./repos.$REV4 ; then
    echo Creating repos for $REV4 ...
    $SVNADMIN create ./repos.$REV4
    gunzip --to-stdout $DUMP4 | $SVNADMIN load --quiet ./repos.$REV4
fi
# The revision of interest in this repository: r2
$MAILER commit ./repos.$REV4 2 $CONFIG > output.$REV4
if cmp --quiet output.$REV4 ../t2-reference/output.$REV4; then
    echo SUCCESS for $REV4
else
    echo FAIL -- differences detected for $REV4
fi
