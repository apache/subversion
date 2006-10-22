#!/bin/sh
# Subversion po file translation status report generator
# To ensure the script produces accurate statistics, make sure that
# you have run './po-update.sh' first

set -e
cd "`dirname \"$0\"`"/../..
branch_name=`svn info | sed -n '/^URL:/s@.*/svn/\(.*\)@\1@p'`
wc_version=`svnversion subversion/po | sed -e 's/[MS]//g'`

echo "
Translation status report for revision $wc_version ($branch_name)

============================================================================"

printf "%8s %7s %7s %7s %7s\n" lang untrans fuzzy trans obs

cd subversion/po
for i in *.po ; do
  translated=`msgattrib --translated $i \
    | grep -E '^msgid *"' | sed 1d | wc -l`
  untranslated=`msgattrib --untranslated $i \
    | grep -E '^msgid *"' | sed 1d | wc -l`
  fuzzy=`msgattrib --only-fuzzy $i \
    | grep -E '^msgid *"' | sed 1d | wc -l`
  obsolete=`msgattrib --only-obsolete $i \
    | grep -E '^msgid *"' | sed 1d | wc -l`

  if ! msgfmt --check-format -o /dev/null $i ; then
      printf "%8s %s\n" $i "FAILS GNU msgfmt --check-format"
  else
      printf "%8s %7d %7d %7d %7d" $i $untranslated $fuzzy $translated $obsolete
  fi

  if test -z "`svn status $i | grep -E '^\?'`" ; then
      echo
  else
      echo ' (not in repository)'
  fi
done
