#! /bin/sh

# This test searches $PATH for a version of `diff' that supports -u.
#
# Usage:  gnu-diff.sh AC-HELPERS-DIR
# 
#    The argument AC-HELPERS-DIR is required.  It tells check-diff.sh
#    where to find its test input files.
#
# Output: either prints the full path of valid `diff' program,
#         or "" if none is found.
#

if test "$1" = ""; then
  echo "usage:  check-diff.sh <ac-helpers-dir>"
  exit 1
fi

input_1=${1}/check-diff-input-1.txt
input_2=${1}/check-diff-input-2.txt
output=${1}/check-diff-output.tmp

# Loop over $PATH, looking for `diff' binaries

IFS=':'

for searchdir in $PATH; do
    # does $searchdir contain an executable called either `gdiff' or `diff'?
    for name in gdiff diff; do
        diff=$searchdir/$name
        if test -x $diff; then
            # Make absolutely sure there's no output file yet.
            rm -f ${output}

            $diff -u ${input_1} ${input_2} > ${output} 2>/dev/null

            # If there's an output file with non-zero size, then this
            # diff supported the "-u" flag, so we're done.
            if [ -s ${output} ]; then
              foundit=yes
            else
              foundit=no
            fi

            ################ Note: How To Test For GNU Diff: #################
            #                                                                #
            # Right now, we only test that diff supports "-u", which is all  #
            # we care about ("svn diff" passes -u by default).  But if we    #
            # someday want pure GNU diff again, it's an easy tweak to make.  #
            # Just use a construction similar to the if-else-fi above, but   #
            # change the condition to:                                       #
            #                                                                #
            # grep "\\ No newline at end of file" ${output} > /dev/null 2>&1 #
            #                                                                #
            # (There are options to suppress grep's output, but who          #
            # knows how portable they are, so just redirect instead.)        #
            #                                                                #
            # This will test for a non-broken GNU diff, because the          #
            # input files are constructed to set off the special             #
            # handling GNU diff has for files that don't end with \n.        #
            #                                                                #
            # Why would we care?  Well, we used to check very carefully for  #
            # a non-broken version of GNU diff, because at that time we      #
            # used `diff' not `diff3' for updates.  On FreeBSD, there was    #
            # a version of GNU diff that had been modified to remove the     #
            # special support for files that don't end in \n.                #
            #                                                                #
            # Don't ask my why, but somehow I think there's a chance we      #
            # might one day again need GNU diff on the client side.  If that #
            # ever happens, remember to read this note. :-)                  #
            #                                                                #
            ##################################################################
            
            # Clean up
            rm -f ${output}*

            if test "$foundit" = "yes"; then
              echo $diff
              exit
            fi
        fi
    done
done

echo ""
