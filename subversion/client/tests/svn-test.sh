#!/bin/sh

SVN_PROG=../svn
TEST_DIR_1=t1
TEST_DIR_2=t2
COMMIT_RESULTFILE_1=commit-1.xml
ANCESTOR_PATH=anni       # See if Greg Stein notices. :-) 

# Remove the testing tree
rm -rf ${TEST_DIR_1} ${TEST_DIR_2} ${COMMIT_RESULTFILE_1}

echo

##### Run tests: #####

### Checking out.
echo "Checking out ${TEST_DIR_1}."
${SVN_PROG} checkout                                      \
      -d ${TEST_DIR_1}                                    \
      --xml-file ../../tests-common/xml/co1-inline.xml    \
      --revision 1                                         \
      --ancestor-path ${ANCESTOR_PATH}

### Copy the pristine checked-out tree, so we can test updates later.
cp -R -p ${TEST_DIR_1} ${TEST_DIR_2}

### Modify some existing files.
echo "Modifying ${TEST_DIR_1}/A/D/G/pi."
echo "fish" >> ${TEST_DIR_1}/A/D/G/pi

echo "Modifying ${TEST_DIR_1}/A/mu."
echo "moo" >> ${TEST_DIR_1}/A/mu

### Add.
echo "Adding ${TEST_DIR_1}/newfile1."
touch ${TEST_DIR_1}/newfile1
echo "This is added file newfile1."
${SVN_PROG} add ${TEST_DIR_1}/newfile1

echo "Adding ${TEST_DIR_1}/A/B/E/newfile2."
touch ${TEST_DIR_1}/A/B/E/newfile2
echo "This is added file newfile2."
${SVN_PROG} add ${TEST_DIR_1}/A/B/E/newfile2

### Delete.
echo "Deleting versioned file A/D/H/omega, with --force."
${SVN_PROG} delete --force ${TEST_DIR_1}/A/D/H/omega

# echo "Deleting added files A/B/E/newfile2, without --force."
# ${SVN_PROG} delete ${TEST_DIR_1}/A/B/E/newfile2
 
### Commit.
echo "Committing changes in ${TEST_DIR_1}."
(cd ${TEST_DIR_1};                                               \
 ../${SVN_PROG} commit --xml-file ../${COMMIT_RESULTFILE_1} --revision 2;   \
 cd ..)

### Show the results of the commit to the user.
# cat ${COMMIT_RESULTFILE_1}

### Update.
echo "Updating ${TEST_DIR_2} from changes in ${TEST_DIR_1}."
(cd ${TEST_DIR_2};                                           \
 ../${SVN_PROG} update --xml-file ../${COMMIT_RESULTFILE_1}  \
                --revision 2;                                 \
 cd ..)

### Diff the two trees.  The only differences should be in timestamps
### and some of the version numbers (see README), so ignore those.
# diff -r t1 t2 | grep -v timestamp | grep -v version
