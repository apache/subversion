#! /bin/sh
#
# USAGE: get-neon-ver.sh NEON-DIRECTORY
#

conf=$1/macros/neon.m4
oldconf=$1/configure.in

major=`sed -n '/NEON_VERSION_MAJOR=/s/.*=//p' $conf`

# older versions of Neon
if test "$major" = ""; then
  vsn=`sed -n '/NEON_VERSION=/s/.*=//p' $oldconf`
  echo $vsn
  exit 0
fi

# current Neon releases
minor=`sed -n '/NEON_VERSION_MINOR=/s/.*=//p' $conf`
release=`sed -n '/NEON_VERSION_RELEASE=/s/.*=//p' $conf`
echo $major.$minor.$release
