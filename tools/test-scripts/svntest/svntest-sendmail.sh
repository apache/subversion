#!/bin/sh

EXEC_PATH="`dirname $0`"
BUILD_TYPE="$1"
RA_TYPE="$2"
FS_TYPE="$3"
BUILD_STAT="$4"

# Source the configuration file.
. "$EXEC_PATH/svntest-config.sh"

# Compute local vars
LOG_FILE="$LOG_FILE_PREFIX.$BUILD_TYPE"
TEST="`$GUESS` $BUILD_TYPE"
REV="`$SVN st -v $SVN_SOURCE/README | $CUT -c 12-17 | $SED -e 's/^ *//'`"
test -z "$RA_TYPE" || {
    LOG_FILE="$LOG_FILE.$RA_TYPE"
    TEST="$TEST $RA_TYPE"
}
test -z "$FS_TYPE" || {
    LOG_FILE="$LOG_FILE.$FS_TYPE"
    TEST="$TEST $FS_TYPE"
}

# The log file must exist
if [ ! -f $LOG_FILE ]
then
    $SENDMAIL -t <<EOF
From: $FROM
Subject: ERROR: svn $REVPREFIX$REV ($TEST)
To: $ERROR_TO

Missing log file: $LOG_FILE
EOF
    exit 1
fi

# The status may only be PASS or FAIL or NOOP
if [ "$BUILD_STAT" != "PASS" -a "$BUILD_STAT" != "FAIL" -a "$BUILD_STAT" != "NOOP" ]
then
    $SENDMAIL -t <<EOF
From: $FROM
Subject: ERROR: svn $REVPREFIX$REV ($TEST)
To: $ERROR_TO

Invalid build status: $BUILD_STAT
EOF
    exit 1
fi

SUBJECT="svn $REVPREFIX$REV: $BUILD_STAT ($TEST)"
# Send the No-Op mail
if [ "$BUILD_STAT" = "NOOP" ]
then
    $SENDMAIL -t <<EOF
From: $FROM
Subject: $SUBJECT
To: $TO

$REVPREFIX$REV: There is nothing to test.
EOF
    exit 0
fi

# Send the status mail
TESTS_LOG_FILE="$LOG_FILE_DIR/tests.$BUILD_TYPE.$RA_TYPE.$FS_TYPE.log.gz"

if [ "$BUILD_STAT" = "PASS" -o ! -f "$TESTS_LOG_FILE" ]
then
    $EXEC_PATH/svntest-sendmail-generic.sh "$TO" "$REPLY_TO" "$SUBJECT" \
        "$LOG_FILE"
else
    $EXEC_PATH/svntest-sendmail-generic.sh "$TO" "$REPLY_TO" "$SUBJECT" \
        "$LOG_FILE" "$TESTS_LOG_FILE"
fi

$RM_F "$TESTS_LOG_FILE"
