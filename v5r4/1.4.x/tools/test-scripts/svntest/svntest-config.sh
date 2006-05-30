#!/bin/sh

# Directory under which all testing happens.  Many of the variables
# after this are subdirectories of this directory.
TEST_ROOT="${HOME}/svn"

# Installation directory.  Everything done under this is temporary and
# may be wiped out between test runs.  This normally lives inside
# ${TEST_ROOT}/.
INST_DIR="${TEST_ROOT}/inst"

# Paths to sources (usually working copies) for Subversion and its
# dependencies, e.g., APR, APR-UTIL, HTTPD.  
#
# For example, $SVN_SOURCE might point to a working copy checked out
# from http://svn.collab.net/repos/svn/trunk/, or from
# http://svn.collab.net/repos/svn/branches/SOME_RELEASE_BRANCH/.
# Similar choices apply to the other source paths.
#
#   ****************************** WARNING ******************************
#   | The svntest system will clean out all non-versioned data from     |
#   | these working copies.  Don't put anything in them you wouldn't be |
#   | comfortable losing.                                               |
#   ****************************** WARNING ******************************
#
# The source trees should live in $TEST_ROOT, but the installation for
# each project will go into $INST_DIR/<proj_name>.  Note that the
# paths you use here will probably matter when you're writing your
# $CONFIG_PREFIX.<proj_name> files (discussed later in this script).
#
# Everything in those directories will be wiped out
# by installation procedure.  See svntest-rebuild-generic.sh

# Recommendation:
# svn co http://svn.collab.net/repos/svn/trunk svn
SVN_NAME=${SVN_NAME:="svn"}
SVN_SOURCE=${SVN_SOURCE:="$TEST_ROOT/$SVN_NAME"}

# Recommendation:
# svn co http://svn.apache.org/repos/asf/apr/apr/branches/0.9.x apr-0.9
APR_NAME=${APR_NAME:="apr-0.9"}
APR_SOURCE=${APR_SOURCE:="$TEST_ROOT/$APR_NAME"}

# Recommendation:
# svn co http://svn.apache.org/repos/asf/apr/apr-util/branches/0.9.x \
#        apr-util-0.9
APU_NAME=${APU_NAME:="apr-util-0.9"}
APU_SOURCE=${APU_SOURCE:="$TEST_ROOT/$APU_NAME"}

# Recommendation:
# svn co http://svn.apache.org/repos/asf/httpd/httpd/branches/2.0.x httpd-2.0
HTTPD_NAME=${HTTPD_NAME:="httpd-2.0"}
HTTPD_SOURCE=${HTTPD_SOURCE:="$TEST_ROOT/$HTTPD_NAME"}

# Set this to always update the SVN_SOURCE tree to a specific revision.
# You have to include the "-r", that is: "FORCE_UP_REV_SVN=-r1729".
# Just leave it empty to default to current head.
FORCE_UP_REV_SVN=

# Options to pass to 'make' (e.g., -j4)
MAKE_OPTS=

# Whether the tests take place on a RAM disk.  RAMDISK=<yes|no>
# (Don't worry: if you set this to "yes", the tests know to clean up
# after themselves so as not to fill up the ramdisk.)
RAMDISK=no

# Whether to pass CLEANUP=true to Makefile test targets.
# INTERMEDIATE_CLEANUP=<yes|no>
INTERMEDIATE_CLEANUP=${RAMDISK:="no"}

# Which build targets to test.
TEST_STATIC=${TEST_STATIC:="yes"}
TEST_SHARED=${TEST_SHARED:="yes"}

# Whether to test the BDB backend.  TEST_FSFS=<yes|no>
TEST_BDB=${TEST_BDB:="yes"}

# Whether to test the FSFS backend.  TEST_FSFS=<yes|no>
TEST_FSFS=${TEST_FSFS:="yes"}

# Whether to test various bindings.
TEST_BINDINGS_SWIG_PERL=${TEST_BINDINGS_SWIG_PERL:="no"}
TEST_BINDINGS_JAVAHL=${TEST_BINDINGS_JAVAHL:="no"}
TEST_BINDINGS_SWIG_PYTHON=${TEST_BINDINGS_SWIG_PYTHON:="no"}
TEST_BINDINGS_SWIG_RUBY=${TEST_BINDINGS_SWIG_RUBY:="no"}

# This must correspond to the Listen directive in your httpd.conf.
RA_DAV_CHECK_ARGS="BASE_URL=http://localhost:52080"

# Port number for svntest's svnserve instance.
SVNSERVE_PORT=52069
RA_SVN_CHECK_ARGS="BASE_URL=svn://localhost:$SVNSERVE_PORT"

# Root of test area for ra_svn, path is relative to the current 
# object (build) directory.
RA_SVN_REPO_ROOT=${RA_SVN_REPO_ROOT:="subversion/tests/cmdline"}

# The test run produces a log file, starting with this prefix.
LOG_FILE_DIR="$TEST_ROOT/logs/$SVN_NAME"
LOG_FILE_PREFIX="$LOG_FILE_DIR/LOG_svntest"

# Prefix for the scripts that invoke configure in each project.
# For example: "config.apr-0.9-shared", "config.subversion-static",
# etc.  You can name these anything you want; svntest will just look
# for things starting with $CONFIG_PREFIX and drive whatever it finds.
CONFIG_PREFIX="config"

# Object directory names.
OBJ_STATIC="obj-st"
OBJ_SHARED="obj-sh"

# E-mail addresses for reporting.  You MUST customize these.
#
# Note: the email address you use in the $FROM variable must be
# subscribed to svn-breakage@subversion.tigris.org.  Otherwise your
# svntest report emails will be mistaken for spam and dropped.
FROM="YOUR_EMAIL_ADDRESS"
TO="svn-breakage@subversion.tigris.org"
ERROR_TO="YOUR_EMAIL_ADDRESS"
REPLY_TO="dev@subversion.tigris.org"

# Paths to utilities.  You may not need to customize these at all.
BIN="/bin"
USRBIN="/usr/bin"
LOCALBIN="/usr/local/bin"
OPTBIN="/opt/bin"
PERLBIN="/usr/bin"

# An independent svn binary that won't be affected by the svntest system.
# This is used for source tree updates.
SVN="/usr/local/bin/svn"

# Path to config.guess (used for generating the mail subject line).
GUESS="/usr/share/libtool/config.guess"

# Path to sendmail.
# ### TODO: Why do we demand sendmail, as opposed to some other MDA?
SENDMAIL="/usr/sbin/sendmail"

# A program that base64-encodes stdin and writes the result to stdout.
# The default is a simple Python program in this directory.
#
BASE64="`dirname $0`/encode-base64.py"

# Other stuff.  You probably don't need to change any of these.
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

# Make sure messages are not translated.
export LANGUAGE=C
export LC_ALL=C

# Branch prefix for the e-mail subject.
REVPREFIX=`$SVN info $SVN_SOURCE | $SED -ne 's@^URL:.*/repos/svn/\(branches/\)*\(.*\)$@\2 r@p'`

# Revision number for the e-mail subject.
REVISION=`$SVN info $SVN_SOURCE | $SED -ne 's@^Revision: \(.*\)$@\1@p'`

### Don't change anything below this line. ###

# Helper functions:

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

# Conditionally mount ramdisk.
# Check that
#    i)  RAMDISK is defined
#    ii) The ramdisk isn't already mounted
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

# Re-initialize ramdisk if it is currently unmounted.
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

