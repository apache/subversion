#!/bin/sh

EXEC_PATH="`dirname $0`"
BUILD_TYPE="$1"
RA_TYPE="$2"
BUILD_STAT="$3"

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

# The log file must exist
if [ ! -f $LOG_FILE ]
then
    $SENDMAIL -t <<EOF
From: $FROM
Subject: "ERROR: svn rev $REV ($TEST)
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
Subject: "ERROR: svn rev $REV ($TEST)
To: $ERROR_TO

Invalid build status: $BUILD_STAT
EOF
    exit 1
fi

# Send the status mail
MAILFILE="/tmp/svntest.$$"
if [ "$BUILD_STAT" = "PASS" ]
then
    $CAT <<EOF > "$MAILFILE"
From: $FROM
Subject: svn rev $REV: $BUILD_STAT ($TEST)
Reply-To: $REPLY_TO
To: $TO

EOF
    $CAT "$LOG_FILE" >> "$MAILFILE"
else
    TESTS_LOG_FILE="$TEST_ROOT/tests.$BUILD_TYPE.$RA_TYPE.log.gz"
    $CAT <<EOF > "$MAILFILE"
From: $FROM
Subject: svn rev $REV: $BUILD_STAT ($TEST)
Reply-To: $REPLY_TO
To: $TO
Content-Type: multipart/mixed; boundary="------------NextPart"

This is a multi-part message in MIME format.
--------------NextPart
Content-Type: text/plain; charset=ascii
Content-Transfer-Encoding: 8bit

EOF
    $CAT "$LOG_FILE" >> "$MAILFILE"
    $CAT <<EOF >> "$MAILFILE"
--------------NextPart
Content-Type: application/x-gzip; name="tests.log.gz"
Content-Transfer-Encoding: base64
Content-Disposition: inline; filename="tests.log.gz"

EOF
    $BASE64_E < "$TESTS_LOG_FILE" >> "$MAILFILE"
    $RM_F "$TESTS_LOG_FILE"
    $CAT <<EOF >> "$MAILFILE"
--------------NextPart--
EOF
fi
$SENDMAIL -t < "$MAILFILE"
$RM_F "$MAILFILE"
