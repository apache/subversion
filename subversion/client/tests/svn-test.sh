#!/bin/sh

SVN_PROG=../svn
TEST_DIR_1=t1
TEST_DIR_2=t2
COMMIT_RESULTFILE_NAME=commit
ANCESTOR_PATH=anni       # See if Greg Stein notices. :-) 

# Remove the testing tree
rm -rf ${TEST_DIR_1} ${TEST_DIR_2} ${COMMIT_RESULTFILE_NAME}*

echo

##### Run tests: #####

### Checking out.
echo "Checking out ${TEST_DIR_1}."
${SVN_PROG} checkout                                      \
      -d ${TEST_DIR_1}                                    \
      --xml-file ../../tests-common/xml/co1-inline.xml    \
      --revision 1                                        \
      --ancestor-path ${ANCESTOR_PATH}

### Copy the pristine checked-out tree, so we can test updates later.
cp -R -p ${TEST_DIR_1} ${TEST_DIR_2}

### Modify some existing files.
echo "Modifying ${TEST_DIR_1}/A/D/G/pi."
echo "in t1, adding a second line to A/D/G/pi" >> ${TEST_DIR_1}/A/D/G/pi

echo "Modifying ${TEST_DIR_1}/A/mu."
echo "in t1, adding a second line to A/mu" >> ${TEST_DIR_1}/A/mu

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
(cd ${TEST_DIR_1};                                                   \
 ../${SVN_PROG} commit --xml-file ../${COMMIT_RESULTFILE_NAME}-2.xml \
                --revision 2;                                        \
 cd ..)

### Update.
echo "Updating ${TEST_DIR_2} from changes in ${TEST_DIR_1}."
(cd ${TEST_DIR_2};                                                    \
 ../${SVN_PROG} update --xml-file ../${COMMIT_RESULTFILE_NAME}-2.xml  \
                --revision 2;                                         \
 cd ..)

### Modify some more files.
echo "Modifying ${TEST_DIR_2}/A/D/G/pi."
echo "in t2, adding a third line to A/D/G/pi" >> ${TEST_DIR_2}/A/D/G/pi
echo "in t2, adding a fourth line to A/D/G/pi" >> ${TEST_DIR_2}/A/D/G/pi
echo "in t2, adding a fifth line to A/D/G/pi" >> ${TEST_DIR_2}/A/D/G/pi
echo "in t2, adding a sixth line to A/D/G/pi" >> ${TEST_DIR_2}/A/D/G/pi
echo "in t2, adding a seventh line to A/D/G/pi" >> ${TEST_DIR_2}/A/D/G/pi
echo "in t2, adding a eighth line to A/D/G/pi" >> ${TEST_DIR_2}/A/D/G/pi
echo "in t2, adding a ninth line to A/D/G/pi" >> ${TEST_DIR_2}/A/D/G/pi
echo "in t2, adding a tenth line to A/D/G/pi" >> ${TEST_DIR_2}/A/D/G/pi

echo "Modifying ${TEST_DIR_2}/A/mu."
echo "in t2, adding a third line to A/mu" >> ${TEST_DIR_2}/A/mu
echo "in t2, adding a fourth line to A/mu" >> ${TEST_DIR_2}/A/mu
echo "in t2, adding a fifth line to A/mu" >> ${TEST_DIR_2}/A/mu
echo "in t2, adding a sixth line to A/mu" >> ${TEST_DIR_2}/A/mu
echo "in t2, adding a seventh line to A/mu" >> ${TEST_DIR_2}/A/mu
echo "in t2, adding a eighth line to A/mu" >> ${TEST_DIR_2}/A/mu
echo "in t2, adding a ninth line to A/mu" >> ${TEST_DIR_2}/A/mu
echo "in t2, adding a tenth line to A/mu" >> ${TEST_DIR_2}/A/mu

### Commit.
echo "Committing changes, this time in ${TEST_DIR_2}."
(cd ${TEST_DIR_2};                                                   \
 ../${SVN_PROG} commit --xml-file ../${COMMIT_RESULTFILE_NAME}-3.xml \
                --revision 3;                                        \
 cd ..)


### Update.
echo "Updating ${TEST_DIR_1} from changes in ${TEST_DIR_2}."
(cd ${TEST_DIR_1};                                                   \
 ../${SVN_PROG} update --xml-file ../${COMMIT_RESULTFILE_NAME}-3.xml \
                --revision 3;                                        \
 cd ..)


### Diff the two trees.  The only differences should be in timestamps
### and some of the revision numbers (see README), so ignore those.
### Also, the names of the .diff and .rej files will be different.
echo "Comparing ${TEST_DIR_1} and ${TEST_DIR_2}."
if [ "0" = "`diff .cvsignore .cvsignore -I % -x % 2>&1 >/dev/null; echo $?`" ]
then
  ### We've got a GNU-ish diff that understands -I and -x
  diff -r ${TEST_DIR_1} ${TEST_DIR_2} \
       -I timestamp -I revision -x '*.diff' -x '*.rej'
else
  ### We-ve got a stupid diff and must dig for results
  diff -r ${TEST_DIR_1} ${TEST_DIR_2} |\
       egrep -v '(timestamp|revision|\.diff$|\.rej$)'
fi


### Make some non-overlapping changes in the same files, merge. ###

### Sleep for long enough to make the timestamps differ.
echo "Sleeping, so we are guaranteed a different timestamp."
sleep 1
echo "Done sleeping."


### Make the changes.
sed -e 's/sixth/SIXTH/' < ${TEST_DIR_1}/A/mu > mu.$$.tmp
mv mu.$$.tmp ${TEST_DIR_1}/A/mu
echo "A non-conflicting change." >> ${TEST_DIR_2}/A/mu


### Commit.
echo "Committing changes for merge, from ${TEST_DIR_1}."
(cd ${TEST_DIR_1};                                                   \
 ../${SVN_PROG} commit --xml-file ../${COMMIT_RESULTFILE_NAME}-4.xml \
                --revision 4;                                        \
 cd ..)


### Update.
echo "Updating ${TEST_DIR_2}, merging changes from ${TEST_DIR_1}."
(cd ${TEST_DIR_2};                                                   \
 ../${SVN_PROG} update --xml-file ../${COMMIT_RESULTFILE_NAME}-4.xml \
                --revision 4;                                        \
 cd ..)


