#! /bin/sh

echo "buildcheck: checking installation..."

# autoconf 2.50 or newer
ac_version=`autoconf --version 2>/dev/null|head -1|sed -e 's/^[^0-9]*//' -e 's/[a-z]* *$//'`
if test -z "$ac_version"; then
echo "buildcheck: autoconf not found."
echo "            You need autoconf version 2.50 or newer installed"
echo "            to build Apache from CVS."
exit 1
fi
IFS=.; set $ac_version; IFS=' '
if test "$1" = "2" -a "$2" -lt "50" || test "$1" -lt "2"; then
echo "buildcheck: autoconf version $ac_version found."
echo "            You need autoconf version 2.50 or newer installed."
exit 1
else
echo "buildcheck: autoconf version $ac_version (ok)"
fi

# libtool 1.4 or newer
libtool=$(which glibtool libtool 2>/dev/null | head -1)
lt_pversion=`$libtool --version 2>/dev/null|sed -e 's/^[^0-9]*//' -e 's/[- ].*//'`
if test -z "$lt_pversion"; then
echo "buildcheck: libtool not found."
echo "            You need libtool version 1.4 or newer installed"
exit 1
fi
lt_version=`echo $lt_pversion|sed -e 's/\([a-z]*\)$/.\1/'`
IFS=.; set $lt_version; IFS=' '
lt_status="good"
if test "$1" = "1"; then
   if test "$2" -lt "4"; then
      lt_status="bad"
   fi
fi
if test $lt_status = "good"; then
   echo "buildcheck: libtool version $lt_pversion (ok)"
   exit 0
fi

echo "buildcheck: libtool version $lt_pversion found."
echo "            You need libtool version 1.4 or newer installed"

exit 1
