#!/bin/sh

EXEC_PATH="`dirname $0`"

# Source the configuration file.
. "$EXEC_PATH/svntest-config.sh"

# Remove log files from previous runs
$RM_F "$LOG_FILE_PREFIX.update"
$RM_F "$LOG_FILE_PREFIX.shared"
$RM_F "$LOG_FILE_PREFIX.static"

# Update the repositories
$EXEC_PATH/svntest-update.sh || {
    $EXEC_PATH/svntest-sendmail.sh "update" "FAIL"
    exit
}

# Prime the shared and static log files
echo >> "$LOG_FILE_PREFIX.update"
$CP_F "$LOG_FILE_PREFIX.update" "$LOG_FILE_PREFIX.shared"
$CP_F "$LOG_FILE_PREFIX.update" "$LOG_FILE_PREFIX.static"

# Test shared
$NICE $EXEC_PATH/svntest-rebuild.sh "shared"
test $? = 0 && shared="PASS" || shared="FAIL"

# Test static
$NICE $EXEC_PATH/svntest-rebuild.sh "static"
test $? = 0 && static="PASS" || static="FAIL"

# Send out the mails
$EXEC_PATH/svntest-sendmail.sh "shared" "$shared"
$EXEC_PATH/svntest-sendmail.sh "static" "$static"
