#! /bin/sh
#
# buildcheck.sh: Inspects the build setup to make detection and
# correction of problems an easier process.

# Initialize parameters
VERSION_CHECK="$1"

if test "$VERSION_CHECK" != "--release"; then
  echo "buildcheck: checking installation..."
else
  echo "buildcheck: checking installation for a source release..."
fi

#--------------------------------------------------------------------------
# autoconf 2.50 or newer
#
ac_version=`${AUTOCONF:-autoconf} --version 2>/dev/null|sed -e 's/^[^0-9]*//' -e 's/[a-z]* *$//' -e 1q`
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
if test "$ac_version" = "2.58"; then
  echo "buildcheck: autoconf version 2.58 found."
  echo "            This version of autoconf is broken.  Please install at"
  echo "            least autoconf 2.59 or downgrade to version 2.57 which"
  echo "            is known to work."
  exit 1
fi

echo "buildcheck: autoconf version $ac_version (ok)"

#--------------------------------------------------------------------------
# autoheader 2.50 or newer
#
ah_version=`${AUTOHEADER:-autoheader} --version 2>/dev/null|sed -e 's/^[^0-9]*//' -e 's/[a-z]* *$//' -e 1q`
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
# check that our local copies of files match up with those in APR(UTIL)
#
if test -d ./apr; then
  if cmp -s ./build/ac-macros/find_apr.m4 ./apr/build/find_apr.m4; then
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
  if cmp -s ./build/ac-macros/find_apu.m4 ./apr-util/build/find_apu.m4; then
    :
  else
    echo "buildcheck: local copy of find_apu.m4 does not match APRUTIL's copy."
    echo "            An updated copy of find_apu.m4 may need to be checked in."
  fi
fi

#--------------------------------------------------------------------------
exit 0
