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
test $shared = "PASS" && {
    $NICE $EXEC_PATH/svntest-run.sh "shared" "ra_local"
    test $? = 0 && shared_ra_local="PASS" || shared_ra_local="FAIL"

    $NICE $EXEC_PATH/svntest-run.sh "shared" "ra_svn"
    test $? = 0 && shared_ra_svn="PASS" || shared_ra_svn="FAIL"
}

# Test static
$NICE $EXEC_PATH/svntest-rebuild.sh "static"
test $? = 0 && static="PASS" || static="FAIL"
test $static = "PASS" && {
    $NICE $EXEC_PATH/svntest-run.sh "static" "ra_local"
    test $? = 0 && static_ra_local="PASS" || static_ra_local="FAIL"

    $NICE $EXEC_PATH/svntest-run.sh "static" "ra_svn"
    test $? = 0 && static_ra_svn="PASS" || static_ra_svn="FAIL"
}

# Send out the mails
test $shared = "FAIL" && \
    $EXEC_PATH/svntest-sendmail.sh "shared" "" "$shared"
test $shared = "PASS" && {
    $EXEC_PATH/svntest-sendmail.sh "shared" "ra_local" "$shared_ra_local"
    $EXEC_PATH/svntest-sendmail.sh "shared" "ra_svn" "$shared_ra_svn"
}

test $static = "FAIL" && \
    $EXEC_PATH/svntest-sendmail.sh "static" "" "$static"
test $static = "PASS" && {
    $EXEC_PATH/svntest-sendmail.sh "static" "ra_local" "$static_ra_local"
    $EXEC_PATH/svntest-sendmail.sh "static" "ra_svn" "$static_ra_svn"
}
