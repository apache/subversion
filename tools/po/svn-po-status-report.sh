#!/bin/sh

# Subversion po file translation status report generator

# This file is based on the GNU gettext msgattrib tool
#  for message file filtering

# To make the script work, make sure that:

# 1) the script knows where to find msgattrib
# 2) you have checked out the required revision
# 2) you have run autogen.sh and configure for that revision
# 3) you have run 'make locale-gnu-po-update'


BIN=/bin
USRBIN=/usr/bin

DIRNAME=$USRBIN/dirname
GREP=$BIN/grep
MAKE=$USRBIN/make
PWD=$BIN/pwd
RM=$BIN/rm
SED=$BIN/sed
MSGATTRIB=/usr/local/bin/msgattrib
MSGFMT=/usr/local/bin/msgfmt


SVNDIR=/usr/local/bin
SENDMAIL=/usr/sbin/sendmail
SVN=$SVNDIR/svn
SVNVERSION=$SVNDIR/svnversion
REVISION_PREFIX='r'



EXEC_PATH=`$DIRNAME "$0"`
WC_L='/usr/bin/wc -l'

cd $EXEC_PATH/../..


wc_version=`$SVNVERSION . | $SED -e 's/M//'`
cd subversion/po

echo "

Subversion translation status report for revision $wc_version

============================================================================"


for i in *.po ; do
  translated=`$MSGATTRIB --translated $i | $GREP -E '^msgid *"' | $WC_L`
  untranslated=`$MSGATTRIB --untranslated $i | $GREP -E '^msgid *"' | $WC_L`
  fuzzy=`$MSGATTRIB --only-fuzzy $i | $GREP -E '^msgid *"' | $WC_L`
  obsolete=`$MSGATTRIB --only-obsolete $i | $GREP -E '^msgid *"' | $WC_L`

  echo
  echo "Message counts per status flag for '$i'"
  echo ""
  if ! $MSGFMT --check-format -o /dev/null $i ; then
      echo "FAILS GNU msgfmt --check-format"
  else
      echo "Passes GNU msgfmt --check-format"
  fi
  echo
  echo "$translated translated"
  echo "$untranslated untranslated"
  echo "$fuzzy fuzzy"
  echo "$obsolete obsolete"
  echo

done
echo "
============================================================================"


