#!/bin/sh

EXEC_PATH="`dirname $0`"

# Source the configuration file.
. "$EXEC_PATH/svntest-config.sh"

# Compute local vars
LOG_FILE="$LOG_FILE_PREFIX.update"

CVS_UPDATE_REGEXP_1='^[UPARMC] \(\(docs\)\|\(STATUS\)\|\(CHANGES\)\)'
CVS_UPDATE_REGEXP_2='^[UPARMC] [A-Za-z]'

#
# If update status file contains already
# '0' (== don't rebuild) then update value, 
# otherwise, honor current 'rebuild needed' (1)
# value, and don't update it because
# current rebuild value could be '0'
# arg1 := cvs update log file
# arg2 := rebuild flag file
#
UPDATE_REBUILD_FLAG () {
    local CVS_UP_LOGFILE="$1"
    local RB_FILE="$2"
    local CVS_UP_STATUS=1

    $GREP -v "$CVS_UPDATE_REGEXP_1" "$CVS_UP_LOGFILE" \
	| $GREP -q "$CVS_UPDATE_REGEXP_2" > /dev/null 2>&1
    CVS_UP_STATUS="$?"
    
    if test ! -f "$RB_FILE" 
    then
        echo "1" > "$RB_FILE"
    elif test 0 -eq `$CAT "$RB_FILE"`
    then
        if test "$CVS_UP_STATUS" -eq 0
        then
            echo "1" > "$RB_FILE"
        else
            echo "0" > "$RB_FILE"
        fi
    fi
}

# Update apr, apr-util, httpd
START "update $APR_NAME" "Updating $APR_NAME..."
cd $APR_REPO && $CVS -f -q -z6 update -d -P > "$TEST_ROOT/LOG_up_apr" 2>&1
test $? = 0 || {
    FAIL_LOG "$TEST_ROOT/LOG_up_apr"
    FAIL
}
PASS
UPDATE_REBUILD_FLAG "$TEST_ROOT/LOG_up_apr" "$APR_REPO.rb"

START "update $APU_NAME" "Updating $APU_NAME..."
cd $APU_REPO && $CVS -f -q -z6 update -d -P > "$TEST_ROOT/LOG_up_apu" 2>&1
test $? = 0 || {
    FAIL_LOG "$TEST_ROOT/LOG_up_apu"
    FAIL
}
PASS
UPDATE_REBUILD_FLAG "$TEST_ROOT/LOG_up_apu" "$APU_REPO.rb"

START "update $HTTPD_NAME" "Updating $HTTPD_NAME..."
cd $HTTPD_REPO && $CVS -f -q -z6 update -d -P > "$TEST_ROOT/LOG_up_httpd" 2>&1
test $? = 0 || {
    FAIL_LOG "$TEST_ROOT/LOG_up_httpd"
    FAIL
}
PASS
UPDATE_REBUILD_FLAG "$TEST_ROOT/LOG_up_httpd" "$HTTPD_REPO.rb"

# Update svn
START "update subversion" "Updating Subversion..."
cd $SVN_REPO && $SVN update > "$TEST_ROOT/LOG_up_svn" 2>&1
test $? = 0 || {
    FAIL_LOG "$TEST_ROOT/LOG_up_svn"
    FAIL
}
PASS

# Run autogen.sh
START "autogen.sh" "Running autogen.sh..."
cd $SVN_REPO && ./autogen.sh > "$TEST_ROOT/LOG_svn_autogen" 2>&1
test $? = 0 || {
    FAIL_LOG "$TEST_ROOT/LOG_svn_autogen"
    FAIL
}
PASS
