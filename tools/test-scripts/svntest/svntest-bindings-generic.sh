#!/bin/sh

EXEC_PATH="`dirname $0`"
BUILD_TYPE="$1"
BINDINGS_NAME="$2"
BINDINGS_BUILD="$3"
BINDINGS_INSTALL="$4"
BINDINGS_CHECK="$5"


# Source the configuration file.
. "$EXEC_PATH/svntest-config.sh"

# Compute local vars

LOG_FILE="$LOG_FILE_DIR/LOG_${BINDINGS_NAME}.$BUILD_TYPE"
MY_LOG_FILE_PREF="$LOG_FILE_DIR/LOG_$BINDINGS_NAME"
BUILD="`$GUESS` $BUILD_TYPE"

# Remove interim log files from previous runs
$RM_F "${MY_LOG_FILE_PREF}_build"
$RM_F "${MY_LOG_FILE_PREF}_install"
$RM_F "${MY_LOG_FILE_PREF}_check"

# Initialize log files
$RM_F "${LOG_FILE}.errors.gz"
echo "$BINDINGS_NAME: $REVPREFIX$REVISION on $BUILD" > $LOG_FILE
echo "TIME: $($DATE '+%Y-%m-%d %H:%M:%S %z')" >> $LOG_FILE
echo >> $LOG_FILE

# Check the build type
# We only supports shared buildings at the moment
START "$BINDINGS_NAME::check build type" "$BINDINGS_NAME Checking build type..."
case $BUILD_TYPE in
    shared) OBJ="$OBJ_SHARED" ;;
    *)  echo "$BUILD_TYPE: unknown build type"
        echo "$BUILD_TYPE: unknown build type" >> $LOG_FILE
        FAIL ;;
esac
PASS

echo >> $LOG_FILE
echo "$TEST_ROOT/$CONFIG_PREFIX.$BUILD_TYPE:" >> $LOG_FILE
$CAT "$TEST_ROOT/$CONFIG_PREFIX.$BUILD_TYPE" >> $LOG_FILE
echo >> $LOG_FILE

# Reinitialize ramdisk, if it is not OK
if test "xyes" = "x$RAMDISK";
then
    reinitialize_ramdisk
fi

# Build
START "$BINDINGS_NAME::build" "$BINDINGS_NAME::build..."
cd $TEST_ROOT/$OBJ
$MAKE $BINDINGS_BUILD > "${MY_LOG_FILE_PREF}_build" 2>&1
test $? = 0 || {
    FAIL_LOG "${MY_LOG_FILE_PREF}_build"
    $CAT "${MY_LOG_FILE_PREF}_build" | $GZIP_C > ${LOG_FILE}.errors.gz
    FAIL
}
PASS


test ! -z "$BINDINGS_INSTALL" && { 
   # Install
   START "$BINDINGS_NAME::install" "$BINDINGS_NAME::install..."
   cd $TEST_ROOT/$OBJ
   $MAKE  $BINDINGS_INSTALL > "${MY_LOG_FILE_PREF}_install" 2>&1
   test $? = 0 || {
       FAIL_LOG "${MY_LOG_FILE_PREF}_install"
       $CAT "${MY_LOG_FILE_PREF}_build" "${MY_LOG_FILE_PREF}_install" \
           | $GZIP_C > ${LOG_FILE}.errors.gz
       FAIL
   }
   PASS
}

test ! -z "$BINDINGS_CHECK" && { 
   # Run tests
   START "$BINDINGS_NAME::check" "$BINDINGS_NAME::check..."
   cd $TEST_ROOT/$OBJ
   $MAKE  $BINDINGS_CHECK > "${MY_LOG_FILE_PREF}_check" 2>&1
   test $? = 0 || {
       FAIL_LOG "${MY_LOG_FILE_PREF}_check"
       $CAT "${MY_LOG_FILE_PREF}_build" \
            "${MY_LOG_FILE_PREF}_install" "${MY_LOG_FILE_PREF}_check" \
          | $GZIP_C > ${LOG_FILE}.errors.gz
       FAIL
   }
   PASS
   echo "" >> $LOG_FILE
   echo "Actual results follow:" >> $LOG_FILE 
   $CAT "${MY_LOG_FILE_PREF}_check" >> $LOG_FILE
}

echo "TIME: $($DATE '+%Y-%m-%d %H:%M:%S %z')" >> $LOG_FILE
exit 0
