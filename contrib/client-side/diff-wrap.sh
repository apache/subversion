#!/bin/sh
#
# A wrapper for invoking a thrid-party diff program (e.g. Mac OS X opendiff)
# from 'svn diff --diff-cmd=diff-wrap.sh ARGS... > /dev/null'.
#
# $HeadURL$
# $LastChangedDate$
# $LastChangedBy$
# $LastChangedRevision$

if [ $# -lt 2 ]; then
    echo "usage: $0 [ignored args...] file1 file2" >&2
    exit 1
fi

# Configure your favoriate diff program here.
DIFF=opendiff

# The last two arguments passed to this script are the paths to the files
# to diff.
while [ $# -gt 2 ]; do
    shift
done

# Call the diff command (change the following line to make sense for your
# merge program).
exec $DIFF $*
