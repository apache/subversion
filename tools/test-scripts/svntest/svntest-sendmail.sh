#!/bin/sh

EXEC_PATH="`dirname $0`"
BUILD_TYPE="$1"
BUILD_STAT="$2"

# Source the configuration file.
. "$EXEC_PATH/svntest-config.sh"

# Compute local vars
LOG_FILE="$LOG_FILE_PREFIX.$BUILD_TYPE"
TEST="`$GUESS` $BUILD_TYPE"
REV="`$SVN st -v $SVN_REPO/README | $CUT -c 12-17 | $SED -e 's/^ *//'`"

# The log file must exist
if [ ! -f $LOG_FILE ]
then
    $MAILX -r "$FROM" -s "ERROR: svn rev $REV ($TEST)" "$ERROR_TO" <<EOF
Missing log file: $LOG_FILE
EOF
    exit 1
fi

# The status may only be PASS or FAIL
if [ "$BUILD_STAT" != "PASS" -a "$BUILD_STAT" != "FAIL" ]
then
    $MAILX -r "$FROM" -s "ERROR: svn rev $REV ($TEST)" "$ERROR_TO" <<EOF
Invalid build status: $BUILD_STAT
EOF
    exit 1
fi

# Send the status mail
MAILFILE="/tmp/svntest.$$"
$CAT <<EOF > "$MAILFILE"
Subject: svn rev $REV: $BUILD_STAT ($TEST)
Reply-To: $REPLY_TO
To: $TO

EOF
$CAT "$LOG_FILE" >> "$MAILFILE"
$MAILX -r "$FROM" -t < "$MAILFILE"
$RM_F "$MAILFILE"
