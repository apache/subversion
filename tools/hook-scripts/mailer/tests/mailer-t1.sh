#!/bin/sh
#
# mailer-t1.sh: test #1 for the mailer.py script
#
# This test generates "email" for each revision in the repository,
# concatenating them into one big blob, which is then compared against
# a known output.
#
# Note: mailer-tweak.py must have been run to make the test outputs
#       consistent and reproducible
#
# USAGE: ./mailer-t1.sh REPOS MAILER-SCRIPT
#

if test "$#" != 2; then
    echo "USAGE: ./mailer-t1.sh REPOS MAILER-SCRIPT"
    exit 1
fi

scripts="`dirname $0`"
scripts="`cd $scripts && pwd`"

glom=$scripts/mailer-t1.current
orig=$scripts/mailer-t1.output
conf=$scripts/mailer.conf
rm -f $glom

export TZ=GST

youngest="`svnlook youngest $1`"
for rev in `python -c "print(\" \".join(map(str, range(1,$youngest+1))))"`; do
  $2 commit $1 $rev $conf >> $glom
done

echo "current mailer.py output in: $glom"

dos2unix $glom

echo diff -q $orig $glom
diff -q $orig $glom && echo "SUCCESS: no differences detected"
