#!/bin/sh

EXEC_PATH="`dirname $0`"

# Source the configuration file.
. "$EXEC_PATH/svntest-config.sh"

# Check that every commands could be found
test -x "$SVN" || { 
    echo "SVN: Not found or not executable [$SVN]"
    exit 1
}
test -x "$GUESS" || { 
    echo "GUESS: Not found or not executable [$GUESS]"
    exit 1
}
test -x "$SENDMAIL" || { 
    echo "SENDMAIL: Not found or not executable [$SENDMAIL]"
    exit 1
}
test -x "$BASE64" || { 
    echo "BASE64: Not found or not executable [$BASE64]"
    exit 1
}
test -x "$CAT" || {
    echo "CAT: Not found or not executable [$CAT]"
    exit 1
}
test -x "$CP" || {
    echo "CP: Not found or not executable [$CP]"
    exit 1
}
test -x "$CUT" || {
    echo "CUT: Not found or not executable [$CUT]"
    exit 1
}
test -x "$GREP" || {
    echo "GREP: Not found or not executable [$GREP]"
    exit 1
}
test -x "$GZIP" || {
    echo "GZIP: Not found or not executable [$GZIP]"
    exit 1
}
test -x "$ID" || {
    echo "ID: Not found or not executable [$ID]"
    exit 1
}
test -x "$KILL" || {
    echo "KILL: Not found or not executable [$KILL]"
    exit 1
}
test -x "$MAKE" || {
    echo "MAKE: Not found or not executable [$MAKE]"
    exit 1
}
test -x "$MKDIR" || {
    echo "MKDIR: Not found or not executable [$MKDIR]"
    exit 1
}
test -x "$MOUNT" || {
    echo "MOUNT: Not found or not executable [$MOUNT]"
    exit 1
}
test -x "$NICE" || {
    echo "NICE: Not found or not executable [$NICE]"
    exit 1
}
test -x "$PS" || {
    echo "PS: Not found or not executable [$PS]"
    exit 1
}
test -x "$RM" || {
    echo "RM: Not found or not executable [$RM]"
    exit 1
}
test -x "$SED" || {
    echo "SED: Not found or not executable [$SED]"
    exit 1
}
test -x "$TAIL" || {
    echo "TAIL: Not found or not executable [$TAIL]"
    exit 1
}
test -x "$TOUCH" || {
    echo "TOUCH: Not found or not executable [$TOUCH]"
    exit 1
}
test -x "$UMOUNT" || {
    echo "UMOUNT: Not found or not executable [$UMOUNT]"
    exit 1
}
test -x "$XARGS" || {
    echo "XARGS: Not found or not executable [$XARGS]"
    exit 1
}

# Check various variables
test -z "$TEST_ROOT" && {
    echo "TEST_ROOT: Empty value"
    exit 1
} 
test -z "$INST_DIR" && {
    echo "INST_DIR: Empty value"
    exit 1
}
test -z "$SVN_NAME" && {
    echo "SVN_NAME: Empty value"
    exit 1
}
test -z "$SVN_SOURCE" && {
    echo "SVN_SOURCE: Empty value"
    exit 1
}
test -z "$APR_NAME" && {
    echo "APR_NAME: Empty value"
    exit 1
}
test -z "$APR_SOURCE" && {
    echo "APR_SOURCE: Empty value"
    exit 1
}
test -z "$APU_NAME" && {
    echo "APU_NAME: Empty value"
    exit 1
}
test -z "$APU_SOURCE" && {
    echo "APU_SOURCE: Empty value"
    exit 1
}
test -z "$HTTPD_NAME" && {
    echo "HTTPD_NAME: Empty value"
    exit 1
}
test -z "$HTTPD_SOURCE" && {
    echo "HTTPD_SOURCE: Empty value"
    exit 1
}
test -z "$RAMDISK" && {
    echo "RAMDISK: Empty value"
    exit 1
}
test -z "$RA_DAV_CHECK_ARGS" && {
    echo "RA_DAV_CHECK_ARGS: Empty value"
    exit 1
}
test -z "$LOG_FILE_PREFIX" && {
    echo "LOG_FILE_PREFIX: Empty value"
    exit 1
}
test -z "$CONFIG_PREFIX" && {
    echo "CONFIG_PREFIX: Empty value"
    exit 1
}
test -z "$OBJ_STATIC" && {
    echo "OBJ_STATIC: Empty value"
    exit 1
}
test -z "$OBJ_SHARED" && {
    echo "OBJ_SHARED: Empty value"
    exit 1
}
test -z "$FROM" && {
    echo "FROM: Empty value"
    exit 1
}
test -z "$TO" && {
    echo "TO: Empty value"
    exit 1
}
test -z "$ERROR_TO" && {
    echo "ERROR_TO: Empty value"
    exit 1
}
test -z "$REPLY_TO" && {
    echo "REPLY_TO: Empty value"
    exit 1
}

exit 0
