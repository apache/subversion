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
    $EXEC_PATH/svntest-sendmail.sh "update" "" "" "FAIL"
    exit
}

# conditionally rebuild apr, apr-util and httpd
$EXEC_PATH/svntest-rebuild-generic.sh "$APR_NAME" "$APU_NAME" "$MAKE_OPTS" || {
    $EXEC_PATH/svntest-sendmail.sh "update" "" "" "FAIL"
    exit
}
$EXEC_PATH/svntest-rebuild-generic.sh "$APU_NAME" "$HTTPD_NAME" "$MAKE_OPTS" || {
    $EXEC_PATH/svntest-sendmail.sh "update" "" "" "FAIL"
    exit
}
# httpd won't build with parallel make
$EXEC_PATH/svntest-rebuild-generic.sh "$HTTPD_NAME" "" "" || {
    $EXEC_PATH/svntest-sendmail.sh "update" "" "" "FAIL"
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
    $NICE $EXEC_PATH/svntest-run.sh "shared" "ra_local" "bdb"
    test $? = 0 && sh_ra_local="PASS" || sh_ra_local="FAIL"

    $NICE $EXEC_PATH/svntest-run.sh "shared" "ra_local" "fsfs"
    test $? = 0 && sh_ra_local="PASS" || sh_ra_local="FAIL"

    $NICE $EXEC_PATH/svntest-run.sh "shared" "ra_svn" "bdb"
    test $? = 0 && sh_ra_svn="PASS" || sh_ra_svn="FAIL"

    $NICE $EXEC_PATH/svntest-run.sh "shared" "ra_svn" "fsfs"
    test $? = 0 && sh_ra_svn="PASS" || sh_ra_svn="FAIL"

    $NICE $EXEC_PATH/svntest-run.sh "shared" "ra_dav" "bdb"
    test $? = 0 && sh_ra_dav="PASS" || sh_ra_dav="FAIL"

    $NICE $EXEC_PATH/svntest-run.sh "shared" "ra_dav" "fsfs"
    test $? = 0 && sh_ra_dav="PASS" || sh_ra_dav="FAIL"
}

# Test static
$NICE $EXEC_PATH/svntest-rebuild.sh "static"
test $? = 0 && static="PASS" || static="FAIL"
test $static = "PASS" && {
    $NICE $EXEC_PATH/svntest-run.sh "static" "ra_local" "bdb"
    test $? = 0 && st_ra_local="PASS" || st_ra_local="FAIL"

    $NICE $EXEC_PATH/svntest-run.sh "static" "ra_local" "fsfs"
    test $? = 0 && st_ra_local="PASS" || st_ra_local="FAIL"

    $NICE $EXEC_PATH/svntest-run.sh "static" "ra_svn" "bdb"
    test $? = 0 && st_ra_svn="PASS" || st_ra_svn="FAIL"

    $NICE $EXEC_PATH/svntest-run.sh "static" "ra_svn" "fsfs"
    test $? = 0 && st_ra_svn="PASS" || st_ra_svn="FAIL"

# We have to figure out how the static build of mod_dav_svn should
# be done, and if it is worth the trouble or not.
#   $NICE $EXEC_PATH/svntest-run.sh "static" "ra_dav" "bdb"
#   test $? = 0 && st_ra_dav="PASS" || st_ra_dav="FAIL"
#   $NICE $EXEC_PATH/svntest-run.sh "static" "ra_dav" "fsfs"
#   test $? = 0 && st_ra_dav="PASS" || st_ra_dav="FAIL"
}

# Send out the mails
test $shared = "FAIL" && \
    $EXEC_PATH/svntest-sendmail.sh "shared" "" "" "$shared"
test $shared = "PASS" && {
    $EXEC_PATH/svntest-sendmail.sh "shared" "ra_local" "bdb"  "$sh_ra_local"
    $EXEC_PATH/svntest-sendmail.sh "shared" "ra_local" "fsfs" "$sh_ra_local"
    $EXEC_PATH/svntest-sendmail.sh "shared" "ra_svn"   "bdb"  "$sh_ra_svn"
    $EXEC_PATH/svntest-sendmail.sh "shared" "ra_svn"   "fsfs" "$sh_ra_svn"
    $EXEC_PATH/svntest-sendmail.sh "shared" "ra_dav"   "bdb"  "$sh_ra_dav"
    $EXEC_PATH/svntest-sendmail.sh "shared" "ra_dav"   "fsfs" "$sh_ra_dav"
}

test $static = "FAIL" && \
    $EXEC_PATH/svntest-sendmail.sh "static" "" "" "$static"
test $static = "PASS" && {
    $EXEC_PATH/svntest-sendmail.sh "static" "ra_local" "bdb"  "$st_ra_local"
    $EXEC_PATH/svntest-sendmail.sh "static" "ra_local" "fsfs" "$st_ra_local"
    $EXEC_PATH/svntest-sendmail.sh "static" "ra_svn"   "bdb"  "$st_ra_svn"
    $EXEC_PATH/svntest-sendmail.sh "static" "ra_svn"   "fsfs" "$st_ra_svn"
#   $EXEC_PATH/svntest-sendmail.sh "static" "ra_dav"   "bdb"  "$st_ra_dav"
#   $EXEC_PATH/svntest-sendmail.sh "static" "ra_dav"   "fsfs" "$st_ra_dav"
}
