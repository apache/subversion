#!/bin/sh

EXEC_PATH="`dirname $0`"

# Source the configuration file.
. "$EXEC_PATH/svntest-config.sh"

# Compute local vars
LOG_FILE="$LOG_FILE_PREFIX.update"

CVS_UPDATE_REGEXP_1='^[UPARMC] \(\(docs\)\|\(STATUS\)\|\(CHANGES\)\)'
CVS_UPDATE_REGEXP_2='^[UPARMC] [A-Za-z]'

SVN_UPDATE_REGEXP_1=\
'^[ADUCG]\([ADUCG]\| \) \(\(doc\)\|\(notes\)\|\(www\)\|\(contrib\)'\
'\|\(tools\)\|\(packages\)\|\(STATUS\)\)'

SVN_UPDATE_REGEXP_2='^[ADUCG]\([ADUCG]\| \) [A-Za-z]'

#
# Possible values of the status file:
#    0 == rebuild needed
#    timestamp == timestamp of last build
#
# arg1 := mode of operation <CVS|SVN>
# arg2 := update log file
# arg3 := rebuild flag file
#
UPDATE_REBUILD_FLAG () {
    local MODE="$1"
    local UP_LOGFILE="$2"
    local RB_FILE="$3"
    local UP_STATUS=1

    if [ $MODE = "SVN" ]; then
        REGEXP_1="$SVN_UPDATE_REGEXP_1"
        REGEXP_2="$SVN_UPDATE_REGEXP_2"
    elif [ $MODE = "CVS" ]; then
        REGEXP_1="$CVS_UPDATE_REGEXP_1"
        REGEXP_2="$CVS_UPDATE_REGEXP_2"
    fi

    $GREP -v "$REGEXP_1" "$UP_LOGFILE" \
        | $GREP -q "$REGEXP_2" > /dev/null 2>&1
    UP_STATUS="$?"
    
    if test ! -f "$RB_FILE" 
    then
        echo "0" > "$RB_FILE"
    else
        if test "$UP_STATUS" -eq 0
        then
            echo "0" > "$RB_FILE"
        fi
    fi
}

# Update apr, apr-util, httpd
START "update $APR_NAME" "Updating $APR_NAME..."
cd $APR_REPO && $SVN update > "$TEST_ROOT/LOG_up_apr" 2>&1
test $? = 0 || {
    FAIL_LOG "$TEST_ROOT/LOG_up_apr"
    FAIL
}
PASS
UPDATE_REBUILD_FLAG SVN "$TEST_ROOT/LOG_up_apr" "$APR_REPO.rb"

START "update $APU_NAME" "Updating $APU_NAME..."
cd $APU_REPO && $SVN update > "$TEST_ROOT/LOG_up_apu" 2>&1
test $? = 0 || {
    FAIL_LOG "$TEST_ROOT/LOG_up_apu"
    FAIL
}
PASS
UPDATE_REBUILD_FLAG SVN "$TEST_ROOT/LOG_up_apu" "$APU_REPO.rb"

START "update $HTTPD_NAME" "Updating $HTTPD_NAME..."
cd $HTTPD_REPO && $CVS -f -q -z6 update -d -P > "$TEST_ROOT/LOG_up_httpd" 2>&1
test $? = 0 || {
    FAIL_LOG "$TEST_ROOT/LOG_up_httpd"
    FAIL
}
PASS
UPDATE_REBUILD_FLAG CVS "$TEST_ROOT/LOG_up_httpd" "$HTTPD_REPO.rb"

# Update svn
START "update subversion" "Updating Subversion..."
cd $SVN_REPO && $SVN update > "$TEST_ROOT/LOG_up_svn" 2>&1
test $? = 0 || {
    FAIL_LOG "$TEST_ROOT/LOG_up_svn"
    FAIL
}
PASS
UPDATE_REBUILD_FLAG SVN "$TEST_ROOT/LOG_up_svn" "$SVN_REPO.rb"

# Run autogen.sh
START "autogen.sh" "Running autogen.sh..."
cd $SVN_REPO && ./autogen.sh > "$TEST_ROOT/LOG_svn_autogen" 2>&1
test $? = 0 || {
    FAIL_LOG "$TEST_ROOT/LOG_svn_autogen"
    FAIL
}
PASS
