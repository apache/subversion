#!/bin/sh

EXEC_PATH="`dirname $0`"
BUILD_TYPE="$1"

# Source the configuration file.
. "$EXEC_PATH/svntest-config.sh"

# Compute local vars
LOG_FILE="$LOG_FILE_PREFIX.$BUILD_TYPE"
TEST="`$GUESS` $BUILD_TYPE"
REV="`$SVN st -v $SVN_REPO/README | $CUT -c 12-17 | $SED -e 's/^ *//'`"

# Initialize the log file
echo "TEST: Revision $REV on $TEST" >> $LOG_FILE
echo >> $LOG_FILE

# Check the build type
START "check build type" "Checking build type..."
BUILD_TYPE="$1"
case $BUILD_TYPE in
    shared) OBJ="$OBJ_SHARED" ;;
    static) OBJ="$OBJ_STATIC" ;;
    *)  echo "$BUILD_TYPE: unknown build type"
        echo "$BUILD_TYPE: unknown build type" >> $LOG_FILE
        FAIL ;;
esac
PASS

# Create the object directory
START "create object directory" "Creating object directory..."
$RM_RF $TEST_ROOT/$OBJ >> $LOG_FILE 2>&1 || FAIL
$MKDIR $TEST_ROOT/$OBJ >> $LOG_FILE 2>&1 || FAIL
PASS

# Configure
START "configure" "Configuring..."
echo >> $LOG_FILE
echo "$TEST_ROOT/$CONFIG_PREFIX.$BUILD_TYPE:" >> $LOG_FILE
$CAT "$TEST_ROOT/$CONFIG_PREFIX.$BUILD_TYPE" >> $LOG_FILE

cd $TEST_ROOT/$OBJ
$TEST_ROOT/$CONFIG_PREFIX.$BUILD_TYPE 2>&1 | \
    $TEE "$TEST_ROOT/LOG_svn_configure_$BUILD_TYPE"
test $? = 0 || {
    FAIL_LOG "$TEST_ROOT/LOG_svn_configure_$BUILD_TYPE"
    FAIL
}
PASS

# Build
START "build" "Building..."
cd $TEST_ROOT/$OBJ
$MAKE 2>&1 | $TEE "$TEST_ROOT/LOG_svn_build_$BUILD_TYPE"
test $? = 0 || {
    FAIL_LOG "$TEST_ROOT/LOG_svn_build_$BUILD_TYPE"
    FAIL
}
PASS

# Test
START "check" "Testing..."
cd $TEST_ROOT/$OBJ
$MAKE check 2>&1 | $TEE "$TEST_ROOT/LOG_svn_check_$BUILD_TYPE"
test $? = 0 || {
    FAIL_LOG "$TEST_ROOT/LOG_svn_check_$BUILD_TYPE"
    $CP "tests.log" "$LOG_FILE_PREFIX.log.$BUILD_TYPE.$REV.failed" \
        >> $LOG_FILE 2>&1

    echo >> $LOG_FILE
    echo "tests.log:" >> $LOG_FILE
    $CAT tests.log >> $LOG_FILE 2>&1
    FAIL
}
PASS
