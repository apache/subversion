#! /bin/sh

echo "buildcheck: checking installation..."

# Initialize parameters
NEON_CHECK_CONTROL="$1"

#--------------------------------------------------------------------------
# autoconf 2.50 or newer
#
ac_version=`${AUTOCONF:-autoconf} --version 2>/dev/null|head -1| sed -e 's/^[^0-9]*//' -e 's/[a-z]* *$//'`
if test -z "$ac_version"; then
  echo "buildcheck: autoconf not found."
  echo "            You need autoconf version 2.50 or newer installed."
  exit 1
fi
IFS=.; set $ac_version; IFS=' '
if test "$1" = "2" -a "$2" -lt "50" || test "$1" -lt "2"; then
  echo "buildcheck: autoconf version $ac_version found."
  echo "            You need autoconf version 2.50 or newer installed."
  echo "            If you have a sufficient autoconf installed, but it"
  echo "            is not named 'autoconf', then try setting the"
  echo "            AUTOCONF environment variable.  (See the INSTALL file"
  echo "            for details.)"
  exit 1
fi

echo "buildcheck: autoconf version $ac_version (ok)"

#--------------------------------------------------------------------------
# autoheader 2.50 or newer
#
ah_version=`${AUTOHEADER:-autoheader} --version 2>/dev/null|head -1| sed -e 's/^[^0-9]*//' -e 's/[a-z]* *$//'`
if test -z "$ah_version"; then
  echo "buildcheck: autoheader not found."
  echo "            You need autoheader version 2.50 or newer installed."
  exit 1
fi
IFS=.; set $ah_version; IFS=' '
if test "$1" = "2" -a "$2" -lt "50" || test "$1" -lt "2"; then
  echo "buildcheck: autoheader version $ah_version found."
  echo "            You need autoheader version 2.50 or newer installed."
  echo "            If you have a sufficient autoheader installed, but it"
  echo "            is not named 'autoheader', then try setting the"
  echo "            AUTOHEADER environment variable.  (See the INSTALL file"
  echo "            for details.)"
  exit 1
fi

echo "buildcheck: autoheader version $ah_version (ok)"

#--------------------------------------------------------------------------
# libtool 1.4 or newer
#
libtool=`which glibtool 2>/dev/null`
if test ! -x "$libtool"; then
  libtool=`which libtool`
fi
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
if test $lt_status != "good"; then
  echo "buildcheck: libtool version $lt_pversion found."
  echo "            You need libtool version 1.4 or newer installed"
  exit 1
fi

echo "buildcheck: libtool version $lt_pversion (ok)"

#--------------------------------------------------------------------------
# check for the correct version of Neon
#
NEON_WANTED_REGEX=0.23.?
NEON_LATEST_WORKING_VER=0.23.5
NEON_URL="http://www.webdav.org/neon/neon-${NEON_LATEST_WORKING_VER}.tar.gz"
NEON_TEST_REGEX="$NEON_WANTED_REGEX"
if test "$NEON_CHECK_CONTROL" = "--disable-neon-version-check"; then
  NEON_TEST_REGEX=*
fi
if test -d ./neon; then
  NEON_VERSION="`./ac-helpers/get-neon-ver.sh neon`"
  case "$NEON_VERSION" in
    $NEON_TEST_REGEX)
      ;;
    *)
      echo "buildcheck: neon version $NEON_VERSION found in ./neon/."
      echo "            You need neon $NEON_LATEST_WORKING_VER."
      exit 1
      ;;
  esac
  echo "buildcheck: neon version $NEON_VERSION (ok)"
fi

#--------------------------------------------------------------------------
# check that our local copies of files match up with those in APR(UTIL)
#
if test -d ./apr; then
  if cmp -s ./ac-helpers/find_apr.m4 ./apr/build/find_apr.m4; then
    :
  else
    echo "buildcheck: local copy of find_apr.m4 does not match APR's copy."
    echo "            An updated copy of find_apr.m4 may need to be checked in."
  fi
  if cmp -s ./build/PrintPath ./apr/build/PrintPath; then
    :
  else
    echo "buildcheck: local copy of PrintPath does not match APR's copy."
    echo "            An updated copy of PrintPath may need to be checked in."
  fi
fi

if test -d ./apr-util; then
  if cmp -s ./ac-helpers/find_apu.m4 ./apr-util/build/find_apu.m4; then
    :
  else
    echo "buildcheck: local copy of find_apu.m4 does not match APRUTIL's copy."
    echo "            An updated copy of find_apu.m4 may need to be checked in."
  fi
fi

#--------------------------------------------------------------------------
exit 0
