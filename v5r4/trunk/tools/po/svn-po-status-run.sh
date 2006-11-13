#!/bin/sh
set -e

SENDMAIL=${SENDMAIL:-sendmail}

cd "`dirname \"$0\"`"/../..

output_log="po-status-stdout-log"
error_log="po-status-stderr-log"

rm -f "$output_log" "$error_log"

# prevent conflicts
svn revert --recursive subversion/po 2>>$error_log >>$output_log || \
{
    ###TODO: mail your output!
    exit 1
}

# update && initialize
svn update 2>>$error_log >>$output_log && \
tools/po/po-update.sh 2>>$error_log >>$output_log || \
{
    # mail your output
    echo "Unable to successfully complete; check error log."
    exit 1
}

tools/po/svn-po-status-report.sh -mail | $SENDMAIL -t
