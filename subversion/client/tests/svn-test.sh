#!/bin/sh

SVN_PROG=../svn
TARGET_DIR=this
ANCESTOR_PATH=anni       # See if Greg Stein notices. :-) 

# Remove the testing tree
rm -rf ${TARGET_DIR}

##### Run tests: #####

### Checking out.
echo
echo "Checking out."
${SVN_PROG} checkout                                      \
      -d ${TARGET_DIR}                                    \
      --xml-file ../../tests-common/xml/co1-inline.xml    \
      --version 1                                         \
      --ancestor-path ${ANCESTOR_PATH}

### Adding.
echo
echo "Adding a file."
touch this/newfile1
${SVN_PROG} add this/newfile1

echo
echo "Adding another file."
touch this/A/B/E/newfile2
${SVN_PROG} add this/A/B/E/newfile2

### Updating.
# echo
# echo "Updating one file."
# (cd this; ../${SVN_PROG} update \
#              --xml-file ../../../tests-common/xml/up1a-inline.xml)

# echo
# echo "Updating many files."
# (cd this; ../${SVN_PROG} update \
#              --xml-file ../../../tests-common/xml/up1b-inline.xml)
                      
### Deleting.
echo
echo "Deleting a versioned file, with --force."
${SVN_PROG} delete --force this/A/D/H/omega

echo
echo "Deleting one of the added files, without --force."
${SVN_PROG} delete this/A/B/E/newfile2
 
### Committing.
## Disable commits until they're working.
# echo
# echo "Committing."
# echo
# (cd this; ../${SVN_PROG} commit --xml-file ../commit-1.xml --version 1; cd ..)
