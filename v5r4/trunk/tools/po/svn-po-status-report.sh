#!/bin/sh
# Subversion po file translation status report generator
# To ensure the script produces accurate statistics, make sure that
# you have run './po-update.sh' first

set -e

MAIL_FROM=${MAIL_FROM:-e.huelsmann@gmx.net}
MAIL_TO=${MAIL_TO:-dev@subversion.tigris.org}

cd "`dirname \"$0\"`"/../..
branch_name=`svn info | sed -n '/^URL:/s@.*/svn/\(.*\)@\1@p'`
wc_version=`svnversion subversion/po | sed -e 's/[MS]//g'`

mail=
while [ $# -ge 1 ]; do
  case $1 in
    -mail) mail=yes ;;
    *) echo "E: Unknown argument: '$1'" >&2; exit 1 ;;
  esac
  shift
done

if [ -n "$mail" ]; then
  echo "From: $MAIL_FROM"
  echo "To: $MAIL_TO"
  echo "Subject: [l10n] Translation status for $branch_name r$wc_version"
  echo
fi

echo "Translation status report for revision $wc_version ($branch_name)"
echo
printf "%6s %7s %7s %7s %7s\n" lang untrans fuzzy trans obs
echo "--------------------------------------"

cd subversion/po
for i in *.po ; do
  trans=`msgattrib --translated $i | grep -E '^msgid *"' | sed 1d | wc -l`
  untrans=`msgattrib --untranslated $i | grep -E '^msgid *"' | sed 1d | wc -l`
  fuzzy=`msgattrib --only-fuzzy $i | grep -E '^msgid *"' | sed 1d | wc -l`
  obsolete=`msgattrib --only-obsolete $i | grep -E '^#~ msgid *"' | wc -l`

  if ! msgfmt --check-format -o /dev/null $i ; then
      printf "%6s %s\n" ${i%.po} "FAILS GNU msgfmt --check-format"
  else
      printf "%6s %7d %7d %7d %7d" ${i%.po} $untrans $fuzzy $trans $obsolete
  fi

  if test -z "`svn status $i | grep -E '^\?'`" ; then
      echo
  else
      echo ' (not in repository)'
  fi
done
