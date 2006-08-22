#!/bin/sh
# Subversion po file translation status report generator
# To ensure the script produces accurate statisticks, make sure that
# you have run 'make locale-gnu-po-update'

set -e
cd "`dirname \"$0\"`"/../..
branch_name=`svn info | sed -n '/^URL:/s@.*/svn/\(.*\)@\1@p'`
wc_version=`svnversion subversion/po | sed -e 's/[MS]//g'`

echo "
Translation status report for revision $wc_version ($branch_name/)

============================================================================"

cd subversion/po
for i in *.po ; do
  translated=`msgattrib --translated $i \
    | grep -E '^msgid *"' | sed -n '2~1p' | wc -l`
  untranslated=`msgattrib --untranslated $i \
    | grep -E '^msgid *"' | sed -n '2~1p' | wc -l`
  fuzzy=`msgattrib --only-fuzzy $i \
    | grep -E '^msgid *"' | sed -n '2~1p' | wc -l`
  obsolete=`msgattrib --only-obsolete $i \
    | grep -E '^msgid *"' | sed -n '2~1p' | wc -l`

  echo
  if test -z "`svn status $i | grep -E '^\?'`" ; then
      echo "Status for '$i': in repository"
  else
      echo "Status for '$i': NOT in repository"
      echo " (See the issue tracker 'translations' subcomponent)"
  fi

  echo
  if ! msgfmt --check-format -o /dev/null $i ; then
      echo "   FAILS GNU msgfmt --check-format"
  else
      echo "   Passes GNU msgfmt --check-format"
      echo
      echo "   Statistics:"
      echo "    $obsolete obsolete"
      echo "    $untranslated untranslated"
      echo "    $translated translated, of which"
      echo "       $fuzzy fuzzy"
  fi
  echo "
----------------------------------------------------------------------------"
done
