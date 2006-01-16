#!/bin/sh

EXEC_PATH="`dirname $0`"
BUILD_TYPE="$1"

# Source the configuration file.
. "$EXEC_PATH/svntest-config.sh"

# Compute local vars
LOG_FILE="$LOG_FILE_PREFIX.$BUILD_TYPE"
BUILD="`$GUESS` $BUILD_TYPE"
REV="`$SVN st -v $SVN_SOURCE/README | $CUT -c 12-17 | $SED -e 's/^ *//'`"

# Initialize the log file
echo "BUILD: $REVPREFIX$REV on $BUILD" >> $LOG_FILE
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

# Create the object directory
START "create object directory" "Creating object directory..."
umount_ramdisk "$TEST_ROOT/$OBJ/subversion/tests" >> $LOG_FILE 2>&1 || FAIL 
$RM_RF "$TEST_ROOT/$OBJ" >> $LOG_FILE 2>&1 || FAIL
$MKDIR "$TEST_ROOT/$OBJ" >> $LOG_FILE 2>&1 || FAIL
$MKDIR_P "$TEST_ROOT/$OBJ/subversion/tests" >> $LOG_FILE 2>&1 || FAIL
mount_ramdisk "$TEST_ROOT/$OBJ/subversion/tests" >> $LOG_FILE 2>&1 || FAIL 
PASS

# Configure
START "configure" "Configuring..."
echo >> $LOG_FILE
echo "$TEST_ROOT/$CONFIG_PREFIX.$BUILD_TYPE:" >> $LOG_FILE
$CAT "$TEST_ROOT/$CONFIG_PREFIX.$BUILD_TYPE" >> $LOG_FILE

cd $TEST_ROOT/$OBJ
$TEST_ROOT/$CONFIG_PREFIX.$BUILD_TYPE \
    > "$LOG_FILE_DIR/LOG_svn_configure_$BUILD_TYPE" 2>&1
test $? = 0 || {
    FAIL_LOG "$LOG_FILE_DIR/LOG_svn_configure_$BUILD_TYPE"
    FAIL
}
PASS

# Build
START "build" "Building..."
cd $TEST_ROOT/$OBJ
$MAKE $MAKE_OPTS > "$LOG_FILE_DIR/LOG_svn_build_$BUILD_TYPE" 2>&1
test $? = 0 || {
    FAIL_LOG "$LOG_FILE_DIR/LOG_svn_build_$BUILD_TYPE"
    FAIL
}
PASS


# Install (bc mod_dav_svn.so)
START "install" "Installing..."
cd $TEST_ROOT/$OBJ
$RM_RF "$INST_DIR/$SVN_NAME"
$MAKE install > "$LOG_FILE_DIR/LOG_svn_install_$BUILD_TYPE" 2>&1
test $? = 0 || {
    FAIL_LOG "$LOG_FILE_DIR/LOG_svn_build_$BUILD_TYPE"
    FAIL
}
PASS

START "$SVN_NAME::rebuild flag" "Updating rebuild flag..."
$DATE "+%s" > "$TEST_ROOT/$SVN_NAME.rb" || FAIL
PASS
