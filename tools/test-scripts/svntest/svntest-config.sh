#!/bin/sh

# Make sure messages are not translated.
export LANGUAGE=C
export LC_ALL=C

#
# Root of the test tree
#
TEST_ROOT="${HOME}/svn"

# Installation path, everything under that is considered
# to be temporary
INST_DIR="$TEST_ROOT/inst"

#
# Repository paths and projects name
#
# installation paths are expected to be:
# '$INST_DIR/<proj_name>, so take care of your
# $CONFIG_PREFIX.<proj_name> files.
# Everything in those directories will be wiped out
# by installation procedure. See svntest-rebuild-generic.sh

SVN_NAME=${SVN_NAME:="svn"}
SVN_SOURCE=${SVN_SOURCE:="$TEST_ROOT/$SVN_NAME"}

APR_NAME=${APR_NAME:="apr-0.9"}
APR_SOURCE=${APR_SOURCE:="$TEST_ROOT/$APR_NAME"}

APU_NAME=${APU_NAME:="apr-util-0.9"}
APU_SOURCE=${APU_SOURCE:="$TEST_ROOT/$APU_NAME"}

HTTPD_NAME=${HTTPD_NAME:="httpd-2.0"}
HTTPD_SOURCE=${HTTPD_SOURCE:="$TEST_ROOT/$HTTPD_NAME"}


# Forced update revision for SVN
# format is: FORCE_UP_REV_SVN=-r<rev num>
# empty FORCE_UP_REV_SVN will default to current head
FORCE_UP_REV_SVN=

MAKE_OPTS=

#
# Whether a RAM disk is used to store test data, RAMDISK=<yes|no>
#
RAMDISK=no

#
# Whether to pass CLEANUP=true to Makefile test targets,
# INTERMEDIATE_CLEANUP=<yes|no>
#
INTERMEDIATE_CLEANUP=${RAMDISK:="no"}

#
# Which build targets to test
#
TEST_STATIC=${TEST_STATIC:="yes"}
TEST_SHARED=${TEST_SHARED:="yes"}

#
# Whether to test the BDB backend, TEST_FSFS=<yes|no>
#
TEST_BDB=${TEST_BDB:="yes"}

#
# Whether to test the FSFS backend, TEST_FSFS=<yes|no>
#
TEST_FSFS=${TEST_FSFS:="yes"}

#
# Whether to test various bindings
#
TEST_BINDINGS_SWIG_PERL=${TEST_BINDINGS_SWIG_PERL:="no"}
TEST_BINDINGS_JAVAHL=${TEST_BINDINGS_JAVAHL:="no"}
TEST_BINDINGS_SWIG_PYTHON=${TEST_BINDINGS_SWIG_PYTHON:="no"}
TEST_BINDINGS_SWIG_RUBY=${TEST_BINDINGS_SWIG_RUBY:="no"}

# This should correspond with your httpd Listen directive
RA_DAV_CHECK_ARGS="BASE_URL=http://localhost:52080"

# port number for svntest's svnserve
SVNSERVE_PORT=52069
RA_SVN_CHECK_ARGS="BASE_URL=svn://localhost:$SVNSERVE_PORT"

# Root of test repositories for ra_svn, path is relative to the current 
# object (build) directory
RA_SVN_REPO_ROOT=${RA_SVN_REPO_ROOT:="subversion/tests/cmdline"}

#
# Log file name prefix
#
LOG_FILE_DIR="$TEST_ROOT/logs/$SVN_NAME"
LOG_FILE_PREFIX="$LOG_FILE_DIR/LOG_svntest"

#
# Configure script prefix and object directory names
#
CONFIG_PREFIX="config"
OBJ_STATIC="obj-st"
OBJ_SHARED="obj-sh"

#
# E-mail addresses for reporting
#
FROM="YOUR_EMAIL_ADDRESS"
TO="svn-breakage@subversion.tigris.org"
ERROR_TO="YOUR_EMAIL_ADDRESS"
REPLY_TO="dev@subversion.tigris.org"

#
# Path to utilities
#
BIN="/bin"
USRBIN="/usr/bin"
LOCALBIN="/usr/local/bin"
OPTBIN="/opt/bin"
PERLBIN="/usr/bin"

# Statically linked svn binary (used for repository updates)
SVN="$TEST_ROOT/static/bin/svn"

# Path to config.guess (used for generating the mail subject line)
GUESS="/usr/share/libtool/config.guess"

# Path to sendmail
SENDMAIL="/usr/sbin/sendmail"

# A program that base64-encodes stdin and writes the result to stdout.
# The default is a simple Python program in this directory.
#
BASE64="`dirname $0`/encode-base64.py"

# Other stuff
CAT="$BIN/cat"
CP="$BIN/cp"
CP_F="$CP -f"
CUT="$USRBIN/cut"
DATE="$BIN/date"
GREP="$BIN/grep"
GZIP="$BIN/gzip"
GZIP_C="$GZIP -9c"
ID="$USRBIN/id"
ID_UN="$ID -un"
KILL="$BIN/kill"
MAKE="$USRBIN/make"
MKDIR="$BIN/mkdir"
MKDIR_P="$MKDIR -p"
MOUNT="$BIN/mount"
NICE="$USRBIN/nice"
PS="$BIN/ps"
PS_U="$PS -u"
RM="$BIN/rm"
RM_F="$RM -f"
RM_RF="$RM -rf"
SED="$BIN/sed"
TAIL="$USRBIN/tail"
TAIL_100="$TAIL -n 100"
TOUCH="$USRBIN/touch"
UMOUNT="$BIN/umount"
XARGS="$USRBIN/xargs"

#
# Branch prefix for the e-mail subject
#
REVPREFIX=`$SVN info $SVN_SOURCE | $SED -ne 's@^URL:.*/repos/svn/\(branches/\)*\(.*\)$@\2 r@p'`

#
# Revision number for the e-mail subject
#
REVISION=`$SVN info $SVN_SOURCE | $SED -ne 's@^Revision: \(.*\)$@\1@p'`

#
# Helper functions
#

# Start a test
START() {
    TST="$1"
    echo ""
    echo "$2"
}

# Test failed
FAIL() {
    echo "FAIL: $TST" >> $LOG_FILE
    test -n "$1" && eval "$1" "$@"  # Run cleanup code
    umount_ramdisk "$TEST_ROOT/$OBJ_STATIC/subversion/tests"
    umount_ramdisk "$TEST_ROOT/$OBJ_SHARED/subversion/tests"
    exit 1
}

# Test passed
PASS() {
    echo "PASS: $TST" >> $LOG_FILE
}

# Copy a partial log to the main log file
FAIL_LOG() {
    echo >> $LOG_FILE
    echo "Last 100 lines of the log file follow:" >> $LOG_FILE
    $TAIL_100 "$1" >> $LOG_FILE 2>&1
    if [ "x$REV" = "x" ]
    then
        SAVED_LOG="$1.failed"
    else
        SAVED_LOG="$1.$REV.failed"
    fi
    $CP "$1" "$SAVED_LOG" >> $LOG_FILE 2>&1
    echo "Complete log saved in $SAVED_LOG" >> $LOG_FILE
}

# Mount ramdisk conditionally
# check that
# i)  RAMDISK is defined
# ii) Ramdisk isn't already mounted
mount_ramdisk() {
    local mount_dir="$1"
    if test "xyes" = "x$RAMDISK";
    then
        test -z "$mount_dir" && return 1

        test -f "$mount_dir/.ramdisk" && {
            echo "Warning: ramdisk exists"
            return 0
        }

        $MOUNT "$mount_dir" || return 1
        $TOUCH "$mount_dir/.ramdisk" || return 1
    fi
    return 0
}

umount_ramdisk() {
    local mount_dir="$1"
    if test "xyes" = "x$RAMDISK";
    then
        test -z "$mount_dir" && return 1

        test -f "$mount_dir/.ramdisk" && {
            $UMOUNT "$mount_dir" >> /dev/null 2>&1 || return 1
        }
    fi
    return 0
}

#
# Re-initialize ramdisk if it is currently unmounted.
#
reinitialize_ramdisk () {
    test -x "$TEST_ROOT/$OBJ/subversion/tests/clients" || {
        START "re-initializing ramdisk" "Re-initializing ramdisk"
        mount_ramdisk "$TEST_ROOT/$OBJ/subversion/tests" \
            >> "$LOG_FILE" 2>&1 || FAIL
        cd "$TEST_ROOT/$OBJ"
        $MAKE  mkdir-init > "$LOG_FILE.ramdisk" 2>&1
        test $? = 0 || {
            FAIL_LOG "$LOG_FILE.ramdisk"
            FAIL
        }
        $MAKE $MAKE_OPTS > "$LOG_FILE.ramdisk" 2>&1
        test $? = 0 || {
            FAIL_LOG "$LOG_FILE.ramdisk"
            FAIL
        }
        PASS
    }
}

