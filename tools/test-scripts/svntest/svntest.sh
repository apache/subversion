#!/bin/sh

EXEC_PATH="`dirname $0`"

# Source the configuration file.
. "$EXEC_PATH/svntest-config.sh"

"$EXEC_PATH/svntest-check-configuration.sh" || {
    exit
}

# Remove log files from previous runs
$RM_F "$LOG_FILE_PREFIX.update"
$RM_F "$LOG_FILE_PREFIX.shared"
$RM_F "$LOG_FILE_PREFIX.static"

# Update the repositories
$EXEC_PATH/svntest-update.sh || {
    $EXEC_PATH/svntest-sendmail.sh "update" "FAIL"
    exit
}

# conditionally rebuild apr, apr-util and httpd
$EXEC_PATH/svntest-rebuild-generic.sh "$APR_NAME" "$APU_NAME" "$MAKE_OPTS" || {
    $EXEC_PATH/svntest-sendmail.sh "update" "FAIL"
    exit
}
$EXEC_PATH/svntest-rebuild-generic.sh "$APU_NAME" "$HTTPD_NAME" "$MAKE_OPTS" || {
    $EXEC_PATH/svntest-sendmail.sh "update" "FAIL"
    exit
}
# httpd won't build with parallel make
$EXEC_PATH/svntest-rebuild-generic.sh "$HTTPD_NAME" "" "" || {
    $EXEC_PATH/svntest-sendmail.sh "update" "FAIL"
    exit
}

# add rebuild log files to the main log file
test -f "$LOG_FILE_PREFIX.rebuild-$APR_NAME" && \
    $CAT "$LOG_FILE_PREFIX.rebuild-$APR_NAME" >> "$LOG_FILE_PREFIX.update"
test -f "$LOG_FILE_PREFIX.rebuild-$APU_NAME" && \
    $CAT "$LOG_FILE_PREFIX.rebuild-$APU_NAME" >> "$LOG_FILE_PREFIX.update"
test -f "$LOG_FILE_PREFIX.rebuild-$HTTPD_NAME" && \
    $CAT "$LOG_FILE_PREFIX.rebuild-$HTTPD_NAME" >> "$LOG_FILE_PREFIX.update"

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

    $NICE $EXEC_PATH/svntest-run.sh "shared" "ra_dav"
    test $? = 0 && shared_ra_dav="PASS" || shared_ra_dav="FAIL"
}

# Test static
$NICE $EXEC_PATH/svntest-rebuild.sh "static"
test $? = 0 && static="PASS" || static="FAIL"
test $static = "PASS" && {
    $NICE $EXEC_PATH/svntest-run.sh "static" "ra_local"
    test $? = 0 && static_ra_local="PASS" || static_ra_local="FAIL"

    $NICE $EXEC_PATH/svntest-run.sh "static" "ra_svn"
    test $? = 0 && static_ra_svn="PASS" || static_ra_svn="FAIL"

# We have to figure out how the static build of mod_dav_svn should
# be done, and if it is worth the trouble or not.     
#    $NICE $EXEC_PATH/svntest-run.sh "static" "ra_dav"
#    test $? = 0 && static_ra_dav="PASS" || static_ra_dav="FAIL"
}

# Send out the mails
test $shared = "FAIL" && \
    $EXEC_PATH/svntest-sendmail.sh "shared" "" "$shared"
test $shared = "PASS" && {
    $EXEC_PATH/svntest-sendmail.sh "shared" "ra_local" "$shared_ra_local"
    $EXEC_PATH/svntest-sendmail.sh "shared" "ra_svn" "$shared_ra_svn"
    $EXEC_PATH/svntest-sendmail.sh "shared" "ra_dav" "$shared_ra_dav"
}

test $static = "FAIL" && \
    $EXEC_PATH/svntest-sendmail.sh "static" "" "$static"
test $static = "PASS" && {
    $EXEC_PATH/svntest-sendmail.sh "static" "ra_local" "$static_ra_local"
    $EXEC_PATH/svntest-sendmail.sh "static" "ra_svn" "$static_ra_svn"
#    $EXEC_PATH/svntest-sendmail.sh "static" "ra_dav" "$static_ra_dav"
}
