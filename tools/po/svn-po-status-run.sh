#!/bin/sh
set -e

MAIL_FROM=${MAIL_FROM:-e.huelsmann@gmx.net}
MAIL_TO=${MAIL_TO:-dev@subversion.tigris.org}
SENDMAIL=${SENDMAIL:-sendmail}

cd "`dirname \"$0\"`"/../..
branch_name=`svn info | sed -n '/^URL:/s@.*/svn/\(.*\)@\1@p'`
wc_version=`svnversion subversion/po | sed -e 's/[MS]//g'`

output_log="po-status-stdout-log"
error_log="po-status-stderr-log"

rm "$output_log" "$error_log"

if test -e "config.po" ; then
    configure="./config.po"
elif test -e "config.nice" ; then
    configure="./config.nice"
else
    configure="./configure"
fi

# prevent conflicts
svn revert --recursive subversion/po 2>>$error_log >>$output_log || \
{
    ###TODO: mail your output!
    exit 1
}

if test -e "Makefile" ; then
    # prevent switches or anything from breaking the update
    make clean 2>>$error_log >>$output_log
fi

# update && initialize
svn update 2>>$error_log >>$output_log && \
./autogen.sh 2>>$error_log >>$output_log && \
$configure 2>>$error_log >>$output_log && \
make locale-gnu-po-update 2>>$error_log >>$output_log || \
{
    # mail your output
    echo "Unable to successfully complete; check error log."
    exit 1
}

$SENDMAIL -t <<EOF
From: $MAIL_FROM
To: $MAIL_TO
Subject: [l10n] Translation status for $branch_name r$revision

`tools/po/svn-po-status-report.sh`
EOF
