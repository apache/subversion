#!/bin/sh

SVN_PROG=../../../../clients/cmdline/svn
XML_DIR=../../../xml
if [ "x${1}" = "x" ]; then
    SVN_PROG=`pwd`/${SVN_PROG}
else
    # argument passed by make when builddir != srcdir
    SVN_PROG=${1}/${SVN_PROG}
fi
if [ "x${2}" = "x" ]; then
    XML_DIR=`pwd`/${XML_DIR}
else
    # argument passed by make when builddir != srcdir
    XML_DIR=${2}/${XML_DIR}
fi
TEST_DIR_1=t1
TEST_DIR_2=t2
COMMIT_RESULTFILE_NAME=commit
ANCESTOR_PATH=anni       # See if Greg Stein notices. :-)
			 # notice what? everything seems fine here. ;-)

check_status()
{
    res=$?
    if [ $res -ne 0 ]; then
      echo Oops, problem: ${@-"(no further details)"}
      exit $res
    fi
}

# Remove the testing tree
rm -rf ${TEST_DIR_1} ${TEST_DIR_2} ${COMMIT_RESULTFILE_NAME}*

echo

##### Run tests: #####

### Checking out.
echo "Checking out ${TEST_DIR_1}."
${SVN_PROG} checkout                                      \
      --destination ${TEST_DIR_1}  \
      --xml-file $XML_DIR/co1-inline.xml    \
      --revision 1                                        \
      ${ANCESTOR_PATH}

check_status 1

### Copy the pristine checked-out tree, so we can test updates later.
cp -R -p ${TEST_DIR_1} ${TEST_DIR_2}

### Modify some existing files.
echo "Modifying ${TEST_DIR_1}/A/D/G/pi."
echo "" >> ${TEST_DIR_1}/A/D/G/pi
echo "for commit rev2, second line in A/D/G/pi" >> ${TEST_DIR_1}/A/D/G/pi

echo "Modifying ${TEST_DIR_1}/A/mu."
echo "" >> ${TEST_DIR_1}/A/mu
echo "for commit rev2, second line in A/mu" >> ${TEST_DIR_1}/A/mu

### Add.
echo "Adding ${TEST_DIR_1}/newfile1."
touch ${TEST_DIR_1}/newfile1
echo "This is added file newfile1." >> ${TEST_DIR_1}/newfile1
${SVN_PROG} add ${TEST_DIR_1}/newfile1

check_status 2

echo "Adding ${TEST_DIR_1}/A/B/E/newfile2."
touch ${TEST_DIR_1}/A/B/E/newfile2
echo "This is added file newfile2." >> ${TEST_DIR_1}/A/B/E/newfile2
${SVN_PROG} add ${TEST_DIR_1}/A/B/E/newfile2

check_status 3

### Delete.
echo "Deleting versioned file A/D/H/omega, with --force."
${SVN_PROG} delete --force ${TEST_DIR_1}/A/D/H/omega

check_status 4

echo "Deleting added files A/B/E/newfile2, without --force."
${SVN_PROG} delete ${TEST_DIR_1}/A/B/E/newfile2
 
check_status 5

### Commit.
### This commit should contain the following targets:
###    A/D/G/pi
###    A/mu
###    newfile1
###    A/D/H/omega
### and should therefore be anchored at the root, with targets
### relative to that.
echo "Committing changes in ${TEST_DIR_1}."
(cd ${TEST_DIR_1};                                                \
 ${SVN_PROG} commit --xml-file ../${COMMIT_RESULTFILE_NAME}-2.xml \
             --revision 2 -m "log msg";                           \
)

check_status 6

### Update.
### The commit XML file was anchored at the root, so update from the root.
echo "Updating ${TEST_DIR_2} from changes in ${TEST_DIR_1}."
(cd ${TEST_DIR_2};                                                \
 ${SVN_PROG} update --xml-file ../${COMMIT_RESULTFILE_NAME}-2.xml \
             --revision 2;                                        \
)

check_status 7


### Modify some more files.
echo "Modifying ${TEST_DIR_2}/A/D/G/pi."
echo "for commit rev2, third line in A/D/G/pi" >> ${TEST_DIR_2}/A/D/G/pi
echo "for commit rev2, fourth line in A/D/G/pi" >> ${TEST_DIR_2}/A/D/G/pi
echo "for commit rev2, fifth line in A/D/G/pi" >> ${TEST_DIR_2}/A/D/G/pi
echo "for commit rev2, sixth line in A/D/G/pi" >> ${TEST_DIR_2}/A/D/G/pi
echo "for commit rev2, seventh line in A/D/G/pi" >> ${TEST_DIR_2}/A/D/G/pi
echo "for commit rev2, eighth line in A/D/G/pi" >> ${TEST_DIR_2}/A/D/G/pi
echo "for commit rev2, ninth line in A/D/G/pi" >> ${TEST_DIR_2}/A/D/G/pi
echo "for commit rev2, tenth line in A/D/G/pi" >> ${TEST_DIR_2}/A/D/G/pi

echo "Modifying ${TEST_DIR_2}/A/mu."
echo "for commit rev2, third line in A/mu" >> ${TEST_DIR_2}/A/mu
echo "for commit rev2, fourth line in A/mu" >> ${TEST_DIR_2}/A/mu
echo "for commit rev2, fifth line in A/mu" >> ${TEST_DIR_2}/A/mu
echo "for commit rev2, sixth line in A/mu" >> ${TEST_DIR_2}/A/mu
echo "for commit rev2, seventh line in A/mu" >> ${TEST_DIR_2}/A/mu
echo "for commit rev2, eighth line in A/mu" >> ${TEST_DIR_2}/A/mu
echo "for commit rev2, ninth line in A/mu" >> ${TEST_DIR_2}/A/mu
echo "for commit rev2, tenth line in A/mu" >> ${TEST_DIR_2}/A/mu


### Commit.
### This commit should contain the following targets:
###    A/D/G/pi
###    A/mu
### and should therefore be anchored at A, with targets
### relative to that.
echo "Committing changes, this time from ${TEST_DIR_2}."
(cd ${TEST_DIR_2};                                                \
 ${SVN_PROG} commit --xml-file ../${COMMIT_RESULTFILE_NAME}-3.xml \
             --revision 3 -m "log msg";                           \
)

check_status 8


### Update.
### The commit XML file was anchored at A, so update from there.
echo "Updating ${TEST_DIR_1} from changes in ${TEST_DIR_2}."
(cd ${TEST_DIR_1}/A;                                                 \
 ${SVN_PROG} update --xml-file ../../${COMMIT_RESULTFILE_NAME}-3.xml \
             --revision 3;                                           \
)

check_status 9


### Diff the two trees.  The only differences should be in timestamps
### and some of the revision numbers (see README), so ignore those.
### Also, the names of the .diff and .rej files will be different.
# echo "Comparing ${TEST_DIR_1} and ${TEST_DIR_2}."
# if [ "0" = "`diff .cvsignore .cvsignore -I % -x % 2>&1 >/dev/null; echo $?`" ]
# then
#   ### We've got a GNU-ish diff that understands -I and -x
#   diff -r ${TEST_DIR_1} ${TEST_DIR_2} \
#        -I timestamp -I revision -x '*.diff' -x '*.rej'
# else
#   ### We-ve got a stupid diff and must dig for results
#   diff -r ${TEST_DIR_1} ${TEST_DIR_2} |\
#        egrep -v '(timestamp|revision|\.diff$|\.rej$)'
# fi


### Make some non-overlapping changes in the same files, merge. ###

### Sleep for long enough to make the timestamps differ.
echo "Sleeping, to guarantee a timestamp bump."
sleep 1


### Make the changes.
sed -e 's/sixth/SIXTH/' < ${TEST_DIR_1}/A/mu > mu.$$.tmp
mv mu.$$.tmp ${TEST_DIR_1}/A/mu
echo "for commit rev4, a non-conflicting change" >> ${TEST_DIR_2}/A/mu


### Commit.
### This commit should contain the following targets:
###    A/mu
### and should therefore be anchored at A, with targets
### relative to that.
echo "Committing changes for merge, from ${TEST_DIR_1}."
(cd ${TEST_DIR_1};                                                \
 ${SVN_PROG} commit --xml-file ../${COMMIT_RESULTFILE_NAME}-4.xml \
             --revision 4 -m "log msg";                           \
)

check_status 10


### Update.
### The commit XML file was anchored at A, so update from there.
echo "Updating ${TEST_DIR_2}, merging changes from ${TEST_DIR_1}."
(cd ${TEST_DIR_2}/A;                                                 \
 ${SVN_PROG} update --xml-file ../../${COMMIT_RESULTFILE_NAME}-4.xml \
             --revision 4;                                           \
)

check_status 11

exit 0

