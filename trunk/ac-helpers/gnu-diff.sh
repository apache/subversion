#! /bin/sh

# This test searches $PATH for a version of `diff' that knows how to
# deal with textfiles that don't necessarily end with \n.  In
# particular, this means looking for GNU diff.
#
#   Note:  FreeBSD's `diff' claims to be GNU, but is actually a hacked
#          version that fails this test.  Native versions of `diff' on
#          other Unices probably fail this test as well.  Install GNU
#          patch and GNU diffutils from the ports/packages.
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

patch=$1

# Determine how to tell echo not to print the trailing \n.
# This is similar to Autoconf's @ECHO_C@ and @ECHO_N@; however, we
# don't generate this file from `gnu-diff.sh.in', so we just do
# something similar inline.
case `echo "testing\c"; echo 1,2,3`,`echo -n testing; echo 1,2,3` in
  *c*,-n*) ECHO_N= ECHO_C='
' ECHO_T='	' ;;
  *c*,*  ) ECHO_N=-n ECHO_C= ECHO_T= ;;
  *)      ECHO_N= ECHO_C='\c' ECHO_T= ;;
esac

# Loop over $PATH, looking for `diff' binaries

IFS=':'

for searchdir in $PATH; do
    # does $searchdir contain an executable called either `gdiff' or `diff'?
    for name in gdiff diff; do
	diff=$searchdir/$name
	if test -x $diff; then
	    # create two identical one-line files (no newline endings)
	    echo $ECHO_N "some text, no newline$ECHO_C" > foofile
	    cp foofile foofile2

	    # append to the first file
	    echo $ECHO_N "...extra text, still no newline$ECHO_C" >> foofile

	    # do a diff, create a patch.
	    $diff -u foofile foofile2 > foofile.patch 2>/dev/null

	    # apply the patch to foofile2
	    $patch < foofile.patch >/dev/null 2>&1

	    # the files should be *identical* now.
	    if cmp -s foofile foofile2; then
		identical=yes
	    else
		identical=no
	    fi

	    # cleanup
	    rm -f foofile*

	    if test "$identical" = "yes"; then
		echo $diff
		exit
	    fi
	fi
    done
done

echo ""
