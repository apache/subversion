#!/bin/sh

export PATH=/usr/local/bin:/usr/bin:/bin

BIN=/bin
USRBIN=/usr/bin

DIRNAME=$USRBIN/dirname
PWD=$BIN/pwd
RM=$BIN/rm
SED=$BIN/sed
MAKE=$USRBIN/make


SVNDIR=/usr/local/bin
SENDMAIL=/usr/sbin/sendmail
SVN=$SVNDIR/svn
SVNVERSION=$SVNDIR/svnversion
REVISION_PREFIX='r'



EXEC_PATH="`$DIRNAME $0`"
cd $EXEC_PATH/../..

root_path="$PWD"
ROOT_PARENT_PATH="`$DIRNAME $root_path`"
branch_name="`echo $root_path | $SED -e "s@$ROOT_PARENT_PATH/@@"`"

mail_from='e.huelsmann@gmx.net'
mail_to='dev@subversion.tigris.org'
output_log="$root_path/po-status-stdout-log"
error_log="$root_path/po-status-stderr-log"

$RM $output_log 2>/dev/null
$RM $error_log 2>/dev/null


if test -e "$root_path/config.po" ; then
    CONFIGURE="$root_path/config.po"
elif test -e "$root_path/config.nice" ; then
    CONFIGURE="$root_path/config.nice"
else
    CONFIGURE="$root_path/configure"
fi

# prevent conflicts
$SVN revert --recursive subversion/po 2>>$error_log >>$output_log || \
{
    ###TODO: mail your output!
    exit 1
}
revision="`$SVNVERSION subversion/po`"

if test -e "$root_path/Makefile" ; then
    # prevent switches or anything from breaking the update
    $MAKE clean 2>>$error_log >>$output_log
fi

# update && initialize
$SVN update 2>>$error_log >>$output_log && \
./autogen.sh 2>>$error_log >>$output_log && \
$CONFIGURE 2>>$error_log >>$output_log && \
$MAKE locale-gnu-po-update 2>>$error_log >>$output_log || \
{
    # mail your output
    echo "Unable to successfully complete; check error log."
    exit 1
}



$SENDMAIL -t <<EOF
From: $mail_from
To: $mail_to
Subject: [l10n] Translation status for $branch_name $REVISION_PREFIX$revision

`tools/po/svn-po-status-report.sh`
EOF
