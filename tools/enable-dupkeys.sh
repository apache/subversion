#!/bin/sh
#
# USAGE: enable-dupkeys.sh REPOS_PATH
#
# This script will upgrade an existing repository to enable "duplicates"
# in the 'strings' table. The addition of that flag was added in r1384
# of Subversion. All old/existing databases need to be updated.
#
# Your PATH should refer the BerkeleyDB 4.0 tools (make sure the 4.0
# versions are found before any others). Typically, you can set this
# up without needing to permanent modify your PATH; use the following:
#
#    $ PATH=/usr/local/BerkeleyDB.4.0/bin:$PATH ./enable-dupkeys.sh REPOS_PATH
#
# Note that REPOS_PATH refers to a Subversion repository, not the 'db'
# directory inside the repository.
#
# Also note: the original 'strings' table will be preserved as 'strings.bak'
#


if test "$#" != 1; then
    echo "USAGE: $0 REPOS_PATH"
    echo "  See the comments in the header of this file for more information."
    exit 1
fi

#
# First, verify that we have the proper versions of the tools.
#

VSN=4.0.14

#
# Find the proper db_dump and db_load to run.  On RedHat systems,
# db_dump is called db4_dump and db_load is named db4_load.  First
# check for db4_* and then fall back to db_*.
#

for try in db4_dump db_dump; do
    if $try -V >/dev/null 2>&1; then
        db_dump=$try
        break
    fi
done

if test -z "$db_dump"; then
    echo "$VSN of db4_dump or db_dump cannot be found in your PATH."
    exit 1
fi

for try in db4_load db_load; do
    if $try -V >/dev/null 2>&1; then
        db_load=$try
        break
    fi
done

if test -z "$db_load"; then
    echo "$VSN of db4_load or db_load cannot be found in your PATH."
    exit 1
fi


v="`$db_dump -V`"
tmp="`echo $v | grep $VSN`"
if test -z "$tmp"; then
    echo "$VSN of db_dump is required. You are running:"
    echo "    $v"
    echo ""
    echo "Make sure your path finds the 4.0 versions of the tools."
    exit 1
fi

v="`$db_load -V`"
tmp="`echo $v | grep $VSN`"
if test -z "$tmp"; then
    echo "$VSN of db_load is required. You are running:"
    echo "    $v"
    echo ""
    echo "Make sure your path finds the 4.0 versions of the tools."
    exit 1
fi


#
# Figure out the paths and check some basic conditions
#

strings_old="$1/db/strings"
strings_new="./strings.new"

if test ! -r "$strings_old"; then
    echo "ERROR: Could not read '$strings_old'. Please correct."
    exit 1
fi

db_dir="`cd $1/db && pwd`"
this_dir="`pwd`"
if test "$db_dir" = "$this_dir"; then
    echo "ERROR: Current directory can not be '$1/db'."
    echo "       Please switch to a different directory to perform the conversion."
    exit 1
fi


#
# Do the conversion!
#

echo "Converting '$strings_old' to '$strings_new' ..."
$db_dump "$strings_old" | $db_load -c duplicates=1 "$strings_new"

echo "Preserving '$strings_old' as '$strings_old.bak' ..."
mv "$strings_old" "${strings_old}.bak"

echo "Moving '$strings_new' to '$strings_old' ..."
mv "$strings_new" "$strings_old"

echo "Done."
