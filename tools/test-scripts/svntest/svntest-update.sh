#!/bin/sh

EXEC_PATH="`dirname $0`"

# Source the configuration file.
. "$EXEC_PATH/svntest-config.sh"

# Compute local vars
LOG_FILE="$LOG_FILE_PREFIX.update"

# Update apr, apr-util, httpd-2.0
START "update apr" "Updating APR..."
cd $APR_REPO && $CVS -f -q -z6 update -d -P > "$TEST_ROOT/LOG_up_apr" 2>&1
test $? = 0 || {
    FAIL_LOG "$TEST_ROOT/LOG_up_apr"
    FAIL
}
PASS

START "update apr-util" "Updating APR-UTIL..."
cd $APU_REPO && $CVS -f -q -z6 update -d -P > "$TEST_ROOT/LOG_up_apu" 2>&1
test $? = 0 || {
    FAIL_LOG "$TEST_ROOT/LOG_up_apu"
    FAIL
}
PASS

#START "update httpd-2.0" "Updating Apache..."
#cd $HTTPD_REPO && $CVS -f -q -z6 update -d -P > "$TEST_ROOT/LOG_up_httpd" 2>&1
#test $? = 0 || {
#    FAIL_LOG "$TEST_ROOT/LOG_up_httpd"
#    FAIL
#}
#PASS

# Update svn
START "update subversion" "Updating Subversion..."
cd $SVN_REPO && $SVN update 2>&1 | $TEE "$TEST_ROOT/LOG_up_svn"
test $? = 0 || {
    FAIL_LOG "$TEST_ROOT/LOG_up_svn"
    FAIL
}
PASS

# Run autogen.sh
START "autogen.sh" "Running autogen.sh..."
cd $SVN_REPO && ./autogen.sh 2>&1 | $TEE "$TEST_ROOT/LOG_svn_autogen"
test $? = 0 || {
    FAIL_LOG "$TEST_ROOT/LOG_svn_autogen"
    FAIL
}
PASS
