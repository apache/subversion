#!/bin/sh

### Search through $PATH for a binary called 'diff3' or 'gdiff3' which
### does not add spurious trailing newlines to its output.  Print the
### full path to a working diff3 binary, or "" if none is found.

### This script assumes $1 is a directory that contains particular
### 'mine.txt', 'yours.txt', 'older.txt', and 'result.txt' files
### needed to test merging.

if test "$1" = ""; then
  echo "usage:  gnu-diff3.sh <directory containing merge-testfiles>"
  exit 1
fi
dir=$1

IFS=':'
for searchdir in $PATH; do
    # does $searchdir contain an executable called either `gdiff3' or `diff3'?
    for name in gdiff3 diff3; do
	diff3=$searchdir/$name
	if test -x $diff3; then
            $diff3 -A -m $dir/mine.txt $dir/older.txt $dir/yours.txt > result

	    # the actual and expected merge result-files should be the same.
	    if cmp -s $dir/result.txt result; then
		identical=yes
	    else
		identical=no
	    fi

	    # cleanup
	    rm -f result

	    if test "$identical" = "yes"; then
		echo $diff3
		exit
	    fi
	fi
    done
done

# failed to find a valid diff3
echo ""
