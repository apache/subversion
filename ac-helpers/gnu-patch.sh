#! /bin/sh
#
# Search $PATH for a version of GNU patch.
# Print the full path to this program, else print "".
#

IFS=':'

for searchdir in $PATH; do
    # does $searchdir contain an executable called either `gpatch' or `patch'?
    for name in gpatch patch; do
	patch=$searchdir/$name
	if test -x $patch; then
	    # run `patch --version`
	    if $patch --version 2>&1 | grep -qw GNU; then
		echo $patch
		exit
	    fi
	fi
    done
done

echo ""
