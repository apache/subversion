#!/bin/sh

EXEC_PATH="`dirname $0`"

# Source the configuration file.
. "$EXEC_PATH/svntest-config.sh"

PROJ_NAME="$1"
NEXT_PROJ="$2"
LOCAL_MAKE_OPTS="$3"

test -z "$PROJ_NAME" && exit 1

# Compute local vars
LOG_FILE="$LOG_FILE_PREFIX.rebuild-$PROJ_NAME"

# Initialize the log file
echo "BUILD: $PROJ_NAME" > $LOG_FILE

START "$PROJ_NAME::check rebuild status" \
    "Checking rebuild status of $PROJ_NAME..."
test -f "$TEST_ROOT/$PROJ_NAME.rb" || FAIL
REBUILD_PROJ="`$CAT $TEST_ROOT/$PROJ_NAME.rb`"
PASS

if test $REBUILD_PROJ -eq 0 ; then
    exit 0
fi

# Create the object directory
START "$PROJ_NAME::create build dir" \
    "Creating build directory for $PROJ_NAME..."
$RM_RF $TEST_ROOT/"obj-$PROJ_NAME" >> $LOG_FILE 2>&1 || FAIL
$MKDIR $TEST_ROOT/"obj-$PROJ_NAME" >> $LOG_FILE 2>&1 || FAIL
PASS

# Configure
START "$PROJ_NAME::configure" "Configuring $PROJ_NAME..."
echo >> $LOG_FILE
echo "$TEST_ROOT/$CONFIG_PREFIX.$PROJ_NAME" >> $LOG_FILE
$CAT "$TEST_ROOT/$CONFIG_PREFIX.$PROJ_NAME" >> $LOG_FILE

cd $TEST_ROOT/"obj-$PROJ_NAME"
$TEST_ROOT/$CONFIG_PREFIX.$PROJ_NAME \
    > "$TEST_ROOT/LOG_${PROJ_NAME}_configure" 2>&1
test $? = 0 || {
    FAIL_LOG "$TEST_ROOT/LOG_${PROJ_NAME}_configure"
    FAIL
}
PASS

# Build
START "$PROJ_NAME::build" "Building $PROJ_NAME..."
cd "$TEST_ROOT/obj-$PROJ_NAME"
$MAKE $LOCAL_MAKE_OPTS > "$TEST_ROOT/LOG_${PROJ_NAME}_build" 2>&1
test $? = 0 || {
    FAIL_LOG "$TEST_ROOT/LOG_${PROJ_NAME}_build"
    FAIL
}
PASS

# Installing
START "$PROJ_NAME::install" "Installing $PROJ_NAME..."
cd "$TEST_ROOT/obj-$PROJ_NAME"

$RM_RF "$INST_DIR/$PROJ_NAME" >> $LOG_FILE 2>&1 || FAIL

$MAKE install > "$TEST_ROOT/LOG_${PROJ_NAME}_install" 2>&1
test $? = 0 || {
    FAIL_LOG "$TEST_ROOT/LOG_${PROJ_NAME}_install"
    FAIL
}
PASS

START "$PROJ_NAME::rebuild flag" "Updating rebuild flag..."
echo "0" > "$TEST_ROOT/$PROJ_NAME.rb" || FAIL
# force rebuilding of next depending project
test -z "$NEXT_PROJ" || echo "1" > "$TEST_ROOT/$NEXT_PROJ".rb
PASS

echo >> $LOG_FILE
