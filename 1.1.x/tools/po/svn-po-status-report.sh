#!/bin/sh

# Subversion po file translation status report generator

# This file is based on the GNU gettext msgattrib tool
#  for message file filtering

# To make the script work, make sure that:

# 1) the script knows where to find msgattrib
# 2) you have checked out the required revision
# 2) you have run autogen.sh and configure for that revision
# 3) you have run 'make locale-gnu-po-update'



EXEC_PATH=`dirname "$0"`
MSGATTRIB=msgattrib
GREP=grep
LC='wc -l'

cd $EXEC_PATH/../..


wc_version=`svnversion . | sed -e 's/M//'`
echo "

Subversion translation status report for revision $wc_version

============================================================================"


for i in *.po ; do
  translated=`$MSGATTRIB --translated $i | $GREP -E '^msgid *"' | $LC`
  untranslated=`$MSGATTRIB --untranslated $i | $GREP -E '^msgid *"' | $LC`
  fuzzy=`$MSGATTRIB --only-fuzzy $i | $GREP -E '^msgid *"' | $LC`
  obsolete=`$MSGATTRIB --only-obsolete $i | $GREP -E '^msgid *"' | $LC`


  echo
  echo "Message counts per status flag for '$i'"
  echo ""
  echo "$translated translated"
  echo "$untranslated untranslated"
  echo "$fuzzy fuzzy"
  echo "$obsolete obsolete"
  echo

done
echo "
============================================================================"


