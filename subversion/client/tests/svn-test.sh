#!/bin/sh

SVN_PROG=../svn
TARGET_DIR=this
ANCESTOR_PATH=anni       # See if Greg Stein notices. :-) 

# Remove the testing tree
rm -rf ${TARGET_DIR}

echo
echo "Checking out."
echo

# Checkout a tree
${SVN_PROG} checkout                                      \
      -d ${TARGET_DIR}                                    \
      --xml-file ../../libsvn_wc/tests/checkout-1.delta   \
      --version 1                                         \
      --ancestor-path ${ANCESTOR_PATH}

echo
echo "Committing."
echo

### Disabled commits until they're working.
# Commit from an unchanged tree.
# (cd this; ../${SVN_PROG} commit --xml-file ../commit-1.xml --version 1; cd ..)
