#!/bin/sh

EXEC_PATH="`dirname $0`"
BUILD_TYPE="$1"
RA_TYPE="$2"

# Source the configuration file.
. "$EXEC_PATH/svntest-config.sh"

# Compute local vars
LOG_FILE="$LOG_FILE_PREFIX.$BUILD_TYPE.$RA_TYPE"
TEST="`$GUESS` $BUILD_TYPE $RA_TYPE"
REV="`$SVN st -v $SVN_REPO/README | $CUT -c 12-17 | $SED -e 's/^ *//'`"

# Prime and initialize the log file
$CP_F "$LOG_FILE_PREFIX.$BUILD_TYPE" $LOG_FILE
echo >> $LOG_FILE
echo "TEST: Revision $REV on $TEST" >> $LOG_FILE
echo >> $LOG_FILE

# Check the build type
START "check build type" "Checking build type..."
case $BUILD_TYPE in
    shared) OBJ="$OBJ_SHARED" ;;
    static) OBJ="$OBJ_STATIC" ;;
    *)  echo "$BUILD_TYPE: unknown build type"
        echo "$BUILD_TYPE: unknown build type" >> $LOG_FILE
        FAIL ;;
esac
PASS

# Check the test type
START "check RA type" "Checking RA methd..."
case $RA_TYPE in
    ra_local) CHECK_TARGET="check" ;;
    ra_svn)   CHECK_TARGET="svncheck" ;;
    *)  echo "$RA_TYPE: unknown RA type"
        echo "$RA_TYPE: unknown RA type" >> $LOG_FILE
        FAIL ;;
esac
PASS

# Check that the object directory exists, and that it contains the
# necessary executable files
START "check object directory" "Checking object directory..."
test -d $TEST_ROOT/$OBJ || FAIL; PASS
START "check svn executable" "Checking svn executable..."
test -x $TEST_ROOT/$OBJ/subversion/clients/cmdline/svn || FAIL; PASS
START "check svnadmin executable" "Checking svnadmin executable..."
test -x $TEST_ROOT/$OBJ/subversion/svnadmin/svnadmin || FAIL; PASS
START "check svnlook executable" "Checking svnlook executable..."
test -x $TEST_ROOT/$OBJ/subversion/svnlook/svnlook || FAIL; PASS
START "check svnserve executable" "Checking svnserve executable..."
test -x $TEST_ROOT/$OBJ/subversion/svnserve/svnserve || FAIL; PASS
START "check svnversion executable" "Checking svnversion executable..."
test -x $TEST_ROOT/$OBJ/subversion/svnversion/svnversion || FAIL; PASS

# Prepare the server
case $CHECK_TARGET in
    check)
        # Nothing to do here
        ;;
    svncheck)
        START "run svnserve" "Running svnserve..."
        $TEST_ROOT/$OBJ/subversion/svnserve/svnserve -d \
            -r $TEST_ROOT/$OBJ/subversion/tests/clients/cmdline \
            >> $LOG_FILE 2>&1
        test $? = 0 || FAIL
        PASS

        START "get svnserve pid" "Getting svnserve process ID..."
        USER_NAME="`$ID_UN`"
        SVNSERVE_PID="`$PS_U $USER_NAME | $GREP '[s]vnserve' \
                       | $SED -e 's/^ *//' | $CUT -f 1 -d ' ' -s`"
        test -n "$SVNSERVE_PID" || FAIL
        PASS
        ;;
esac

# Test
START "make $CHECK_TARGET" "Testing $RA_TYPE..."
CHECK_LOG_FILE="$TEST_ROOT/LOG_svn_check_${BUILD_TYPE}_${RA_TYPE}"
cd $TEST_ROOT/$OBJ
$MAKE $CHECK_TARGET > $CHECK_LOG_FILE 2>&1
test $? = 0 || {
    FAIL_LOG $CHECK_LOG_FILE
    $CP "tests.log" "$LOG_FILE_PREFIX.log.$BUILD_TYPE.$RA_TYPE.$REV.failed" \
        >> $LOG_FILE 2>&1

    echo >> $LOG_FILE
    echo "tests.log:" >> $LOG_FILE
    $CAT tests.log >> $LOG_FILE 2>&1
    FAIL
}
PASS

# Kill the server
case $CHECK_TARGET in
    check)
        # Nothing to do here
        ;;
    svncheck)
        START "kill svnserve" "Stopping svnserve..."
        $KILL $SVNSERVE_PID || FAIL
        PASS
        ;;
esac
