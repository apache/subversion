#!/bin/sh

SVN_PROG=../svn
TARGET_DIR=this
ANCESTOR_PATH=anni       # See if Greg Stein notices. :-) 

# Remove the testing tree
rm -rf ${TARGET_DIR}

echo
echo "Checking out."
${SVN_PROG} checkout                                      \
      -d ${TARGET_DIR}                                    \
      --xml-file ../../libsvn_wc/tests/checkout-1.delta   \
      --version 1                                         \
      --ancestor-path ${ANCESTOR_PATH}


echo
echo "Adding a file."
touch this/newfile1
${SVN_PROG} add this/newfile1

echo
echo "Adding another file."
touch this/A/B/E/newfile2
${SVN_PROG} add this/A/B/E/newfile2

echo
echo "Deleting a versioned file, with --force."
${SVN_PROG} delete --force this/A/D/H/omega

echo
echo "Deleting one of the added files, without --force."
${SVN_PROG} delete this/A/B/E/newfile2

### Disable commits until they're working.
# echo
# echo "Committing."
# echo
# (cd this; ../${SVN_PROG} commit --xml-file ../commit-1.xml --version 1; cd ..)
