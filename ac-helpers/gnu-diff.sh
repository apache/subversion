#! /bin/sh

# This test searches $PATH for a version of `diff' that knows how to
# deal with textfiles that don't necessarily end with \n.  In
# particular, this means looking for GNU diff.
#
#   Note:  FreeBSD's `diff' claims to be GNU, but is actually a hacked
#          version that fails this test.  Native versions of `diff' on other
#          Unices probably fail this test as well.
#
#
# Usage:  gnu-diff.sh PATCHPATH
#
#         PATCHPATH is a path to GNU `patch', which is required
#         to accurately test the behavior of `diff'.
# 
# Output: either prints the full path of valid `diff' program,
#         or "" if none is found.
#

if test "$1" = ""; then
  echo "usage:  gnu-diff.sh <patchpath>"
  exit 1
fi

gnu_diff_path=""
gnu_patch_path=$1
pathlist=$PATH
final="no"

# Loop over $PATH, looking for `diff' binaries

while test "$final" != "";  do
    searchdir=`echo $pathlist | sed -e 's/:.*$//'` 
    final=`echo $pathlist | grep :`
    pathlist=`echo $pathlist | sed -e 's/^[^:]*://'`

    # does $searchdir contain an executable called `diff'?
    if test -f ${searchdir}/diff -o -h ${searchdir}/diff; then
        if test -x ${searchdir}/diff; then

            # create two identical one-line files (no newline endings)
            echo -n "some text, no newline" > foofile
            cp foofile foofile2

            # append to the first file
            echo -n "...extra text, still no newline" >> foofile

            # do a diff, create a patch.
            ${searchdir}/diff -u foofile foofile2 > foo.patch

            # apply the patch to foofile2
            ${gnu_patch_path} < foo.patch 2>&1 >/dev/null

            # the files should be *identical* now.
            cmp -s foofile foofile2 2>&1 >/dev/null
            if test $? -eq 0; then
                gnu_diff_path=${searchdir}/diff
            fi

            # cleanup
            rm foofile foofile2 foo.patch *.rej *.orig 2>/dev/null

        fi
    fi
done

echo $gnu_diff_path




