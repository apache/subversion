#! /bin/sh
#
# Search $PATH for a version of GNU patch.
# Print the full path to this program, else print "".
#

gnu_patch_path=""
pathlist=$PATH
final="no"

# Could this loop *be* any uglier?  
# I can't believe it's the 21st century, and I'm using bourne.

while test "$final" != "";  do
    searchdir=`echo $pathlist | sed -e 's/:.*$//'`   # barf.
    final=`echo $pathlist | grep :`
    pathlist=`echo $pathlist | sed -e 's/^[^:]*://'` # where's my (cdr)?

    # does $searchdir contain an executable called `patch'?
    if test -f ${searchdir}/patch -o -h ${searchdir}/patch; then
        if test -x ${searchdir}/patch; then
            # run `patch --version`
            output=`${searchdir}/patch --version 2>/dev/null`
            if test $? != 0; then
                # `patch --version` returned error; keep looking.
                continue
            fi
            # else, look for "GNU" in the output.
            if test "`echo $output | grep GNU`" != "";  then
                gnu_patch_path="${searchdir}/patch"
                break
            fi
        fi
    fi
done

echo $gnu_patch_path
