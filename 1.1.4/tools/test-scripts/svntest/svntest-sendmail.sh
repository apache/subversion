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
REV="`$SVN st -v $SVN_REPO/README | $CUT -c 12-17 | $SED -e 's/^ *//'`"
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

# The status may only be PASS or FAIL
if [ "$BUILD_STAT" != "PASS" -a "$BUILD_STAT" != "FAIL" ]
then
    $SENDMAIL -t <<EOF
From: $FROM
Subject: ERROR: svn $REVPREFIX$REV ($TEST)
To: $ERROR_TO

Invalid build status: $BUILD_STAT
EOF
    exit 1
fi

# Send the status mail
MAILFILE="/tmp/svntest.$$"
NEXT_PART="NextPart-$$"
TESTS_LOG_FILE="$TEST_ROOT/tests.$BUILD_TYPE.$RA_TYPE.$FS_TYPE.log.gz"
$CAT <<EOF > "$MAILFILE"
From: $FROM
Subject: svn $REVPREFIX$REV: $BUILD_STAT ($TEST)
Reply-To: $REPLY_TO
To: $TO
EOF
if [ "$BUILD_STAT" = "PASS" -o ! -f "$TESTS_LOG_FILE" ]
then
    echo "" >> "$MAILFILE"
    $CAT "$LOG_FILE" >> "$MAILFILE"
else
    $CAT <<EOF >> "$MAILFILE"
MIME-Version: 1.0
Content-Type: multipart/mixed; boundary="----------=_$NEXT_PART"

This is a multi-part message in MIME format.
------------=_$NEXT_PART
Content-Type: text/plain; charset=us-ascii
Content-Transfer-Encoding: 8bit

EOF
    $CAT "$LOG_FILE" >> "$MAILFILE"
    $CAT <<EOF >> "$MAILFILE"
------------=_$NEXT_PART
Content-Type: application/x-gzip; name="tests.log.gz"
Content-Transfer-Encoding: base64
Content-Disposition: inline; filename="tests.log.gz"

EOF
    $BASE64_E < "$TESTS_LOG_FILE" >> "$MAILFILE"
    $RM_F "$TESTS_LOG_FILE"
    $CAT <<EOF >> "$MAILFILE"
------------=_$NEXT_PART--
EOF
fi
$SENDMAIL -t < "$MAILFILE"
$RM_F "$MAILFILE"
