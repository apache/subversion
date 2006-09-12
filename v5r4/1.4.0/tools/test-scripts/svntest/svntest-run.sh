#!/bin/sh

EXEC_PATH="`dirname $0`"
BUILD_TYPE="$1"
RA_TYPE="$2"
FS_TYPE="$3"

# Source the configuration file.
. "$EXEC_PATH/svntest-config.sh"

# Compute local vars
LOG_FILE="$LOG_FILE_PREFIX.$BUILD_TYPE.$RA_TYPE.$FS_TYPE"
TEST="`$GUESS` $BUILD_TYPE $RA_TYPE $FS_TYPE"
REV="`$SVN st -v $SVN_SOURCE/README | $CUT -c 12-17 | $SED -e 's/^ *//'`"

# Prime and initialize the log file
$CP_F "$LOG_FILE_PREFIX.$BUILD_TYPE" $LOG_FILE
echo >> $LOG_FILE
echo "TEST: $REVPREFIX$REV on $TEST" >> $LOG_FILE
echo "TIME: $($DATE '+%Y-%m-%d %H:%M:%S %z')" >> $LOG_FILE
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
    ra_dav)   CHECK_TARGET="davcheck" ;;
    *)  echo "$RA_TYPE: unknown RA type"
        echo "$RA_TYPE: unknown RA type" >> $LOG_FILE
        FAIL ;;
esac
PASS

# Check the FS type
START "check FS type" "Checking FS type..."
case $FS_TYPE in
    bdb)  CHECK_ARGS="FS_TYPE=bdb" ;;
    fsfs) CHECK_ARGS="FS_TYPE=fsfs" ;;
    *)  echo "$FS_TYPE: unknown FS type"
        echo "$FS_TYPE: unknown FS type" >> $LOG_FILE
        FAIL ;;
esac
PASS

# Check that the object directory exists, and that it contains the
# necessary executable files
START "check object directory" "Checking object directory..."
test -d $TEST_ROOT/$OBJ || FAIL; PASS
START "check svn executable" "Checking svn executable..."
test -x $INST_DIR/$SVN_NAME/bin/svn || FAIL; PASS
START "check svnadmin executable" "Checking svnadmin executable..."
test -x $INST_DIR/$SVN_NAME/bin/svnadmin || FAIL; PASS
START "check svnlook executable" "Checking svnlook executable..."
test -x $INST_DIR/$SVN_NAME/bin/svnlook || FAIL; PASS
START "check svnserve executable" "Checking svnserve executable..."
test -x $INST_DIR/$SVN_NAME/bin/svnserve || FAIL; PASS
START "check svnversion executable" "Checking svnversion executable..."
test -x $INST_DIR/$SVN_NAME/bin/svnversion || FAIL; PASS

# Build has initially mounted ramdisk for us, but this
# script will at the end to do unmount, so check if it is mounted or not
# and if it is not, do initial fire up for it
if test "xyes" = "x$RAMDISK";
then
    reinitialize_ramdisk
fi

if test "xyes" = "x$INTERMEDIATE_CLEANUP";
then
    # Flag the tests to cleanup after themselves to avoid requiring
    # hundreds of MBs of storage at once for test data.
    CHECK_ARGS="$CHECK_ARGS CLEANUP=1"
fi

# Prepare the server
case $CHECK_TARGET in
    check)
        # Nothing to do here
        ;;
    svncheck)
        START "run svnserve" "Running svnserve..."
        $TEST_ROOT/$OBJ/subversion/svnserve/svnserve -d \
            --listen-port $SVNSERVE_PORT \
            -r $TEST_ROOT/$OBJ/$RA_SVN_REPO_ROOT \
            >> $LOG_FILE 2>&1
        test $? = 0 || FAIL
        PASS

        START "get svnserve pid" "Getting svnserve process ID..."
        USER_NAME="`$ID_UN`"
        SVNSERVE_PID="`$PS_U $USER_NAME | $GREP '[s]vnserve' \
                       | $SED -e 's/^ *//' | $CUT -f 1 -d ' ' -s`"
        test -n "$SVNSERVE_PID" || FAIL
        PASS
        CHECK_ARGS="$CHECK_ARGS $RA_SVN_CHECK_ARGS"
        ;;
    davcheck)
        START "run $HTTPD_NAME" "Running $HTTPD_NAME..."
        $CP_F "$TEST_ROOT/$HTTPD_NAME.conf" \
            "$INST_DIR/$HTTPD_NAME/conf/httpd.conf" || FAIL

        $CP_F "$TEST_ROOT/mod_dav_${SVN_NAME}.conf" \
            "$INST_DIR/$HTTPD_NAME/conf/mod_dav_svn.conf" || FAIL

        "$INST_DIR/$HTTPD_NAME/bin/apachectl" start \
            >> $LOG_FILE 2>&1
        test $? = 0 || FAIL
        PASS
        CHECK_ARGS="$CHECK_ARGS $RA_DAV_CHECK_ARGS"
        ;;
esac

# Kill the server
kill_svnserve() {
    case $CHECK_TARGET in
        check)
            # Nothing to do here
            ;;
        svncheck)
            START "kill svnserve" "Stopping svnserve..."
            $KILL $SVNSERVE_PID || FAIL
            PASS
            ;;
        davcheck)
            START "kill $HTTPD_NAME" "Stopping $HTTPD_NAME..."
            "$INST_DIR/$HTTPD_NAME/bin/apachectl" stop || \
                FAIL
            PASS
            ;;
    esac

    umount_ramdisk "$TEST_ROOT/$OBJ/subversion/tests"
}

# Test
ts_start=`$DATE +"%s"`

START "make $CHECK_TARGET" "Testing $RA_TYPE on $FS_TYPE..."
CHECK_LOG_FILE="$LOG_FILE_DIR/LOG_svn_check_${BUILD_TYPE}_${RA_TYPE}_${FS_TYPE}"
cd $TEST_ROOT/$OBJ
if test "$CHECK_TARGET" = "davcheck";
then
    # At the moment we can't give repository url with
    # make davcheck, so use check & BASE_URL here for the present
    $MAKE check $CHECK_ARGS > $CHECK_LOG_FILE 2>&1
elif test "$CHECK_TARGET" = "svncheck";
then
    $MAKE check $CHECK_ARGS > $CHECK_LOG_FILE 2>&1
else
    $MAKE $CHECK_TARGET $CHECK_ARGS > $CHECK_LOG_FILE 2>&1
fi
test $? = 0 || {
    FAIL_LOG $CHECK_LOG_FILE
    $CP "tests.log" \
        "$LOG_FILE_PREFIX.log.$BUILD_TYPE.$RA_TYPE.$FS_TYPE.$REV.failed" \
        >> $LOG_FILE 2>&1

    # Prepare the log file for the mailer
    $GZIP_C < "tests.log" \
            > "$LOG_FILE_DIR/tests.$BUILD_TYPE.$RA_TYPE.$FS_TYPE.log.gz"
    FAIL kill_svnserve
}
PASS
ts_stop=`$DATE +"%s"`

START "Timer: make $CHECK_TARGET $(($ts_stop - $ts_start)) sec" \
      "Timer: make $CHECK_TARGET $(($ts_stop - $ts_start)) sec"
PASS

kill_svnserve
echo "TIME: $($DATE '+%Y-%m-%d %H:%M:%S %z')" >> $LOG_FILE

