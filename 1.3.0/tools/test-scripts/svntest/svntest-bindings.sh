#!/bin/sh

EXEC_PATH="`dirname $0`"
BUILD_TYPE="$1"

# Source the configuration file.
. "$EXEC_PATH/svntest-config.sh"

"$EXEC_PATH/svntest-check-configuration.sh" || {
    exit
}
BUILD="`$GUESS` $BUILD_TYPE"

#
# A helper function for sending out status emails of bindings
#
send_bindings_email () {
    local BINDING_NAME="$1"
    local BINDING_STATUS="$2"
    local SUBJECT="$BINDING_NAME $REVPREFIX$REVISION: $BINDING_STATUS ($BUILD)"

    $EXEC_PATH/svntest-sendmail-generic.sh \
        "$TO" "$REPLY_TO" "$SUBJECT" \
        "$LOG_FILE_DIR/LOG_${BINDING_NAME}.$BUILD_TYPE" \
        "$LOG_FILE_DIR/LOG_${BINDING_NAME}.$BUILD_TYPE.errors.gz"
}

# With swig-pl run following targets:
# build, check
BINDING_NAME="swig-pl"
test "$TEST_BINDINGS_SWIG_PERL" = "yes" && {
    $NICE $EXEC_PATH/svntest-bindings-generic.sh \
       "$BUILD_TYPE" "$BINDING_NAME" \
       "swig-pl" "" "check-swig-pl"
    if test $? = 0
    then
        send_bindings_email "$BINDING_NAME" "PASS"
    else
        send_bindings_email "$BINDING_NAME" "FAIL"
    fi
}

# With swig-rb run following targets:
# build, check
BINDING_NAME="swig-rb"
test "$TEST_BINDINGS_SWIG_RUBY" = "yes" && {
    $NICE $EXEC_PATH/svntest-bindings-generic.sh \
       "$BUILD_TYPE" "$BINDING_NAME" \
       "swig-rb" "" "check-swig-rb"
    if test $? = 0
    then
        send_bindings_email "$BINDING_NAME" "PASS"
    else
        send_bindings_email "$BINDING_NAME" "FAIL"
    fi
}

# With swig-py run following targets:
# build, install
BINDING_NAME="swig-py"
test "$TEST_BINDINGS_SWIG_PYTHON" = "yes" && {
    $NICE $EXEC_PATH/svntest-bindings-generic.sh \
       "$BUILD_TYPE" "$BINDING_NAME" \
       "swig-py" "install-swig-py" "check-swig-py"
    if test $? = 0
    then
        send_bindings_email "$BINDING_NAME" "PASS"
    else
        send_bindings_email "$BINDING_NAME" "FAIL"
    fi
}

# With JavaHL run following targets:
# build, install, check
BINDING_NAME="java-hl"
test "$TEST_BINDINGS_JAVAHL" = "yes" && {
    $NICE $EXEC_PATH/svntest-bindings-generic.sh \
        "$BUILD_TYPE" "$BINDING_NAME" \
        "javahl" "install-javahl" "check-javahl"
    if test $? = 0
    then
        send_bindings_email "$BINDING_NAME" "PASS"
    else
        send_bindings_email "$BINDING_NAME" "FAIL"
    fi
}
