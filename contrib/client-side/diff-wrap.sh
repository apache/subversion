#!/bin/sh
#
# A wrapper for invoking a thrid-party diff program (e.g. Mac OS X opendiff)
# from 'svn diff --diff-cmd=diff-wrap.sh ARGS... > /dev/null'.
#
# ====================================================================
# Copyright (c) 2007 CollabNet.  All rights reserved.
#
# This software is licensed as described in the file COPYING, which
# you should have received as part of this distribution.  The terms
# are also available at http://subversion.tigris.org/license-1.html.
# If newer versions of this license are posted there, you may use a
# newer version instead, at your option.
#
# This software consists of voluntary contributions made by many
# individuals.  For exact contribution history, see the revision
# history and logs, available at http://subversion.tigris.org/.
# ====================================================================
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
