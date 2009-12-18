#!/bin/sh

# USAGE: ./dist.sh -v VERSION -r REVISION -pr REPOS-PATH
#                  [-alpha ALPHA_NUM|-beta BETA_NUM|-rc RC_NUM|pre PRE_NUM]
#                  [-apr PATH-TO-APR ] [-apru PATH-TO-APR-UTIL] 
#                  [-apri PATH-TO-APR-ICONV] [-neon PATH-TO-NEON]
#                  [-serf PATH-TO-SERF] [-zlib PATH-TO-ZLIB]
#                  [-sqlite PATH-TO-SQLITE] [-zip] [-sign] [-nodeps]
#
#   Create a distribution tarball, labelling it with the given VERSION.
#   The tarball will be constructed from the root located at REPOS-PATH,
#   in REVISION.  For example, the command line:
#
#      ./dist.sh -v 1.4.0 -r ????? -pr branches/1.4.x
#
#   will create a 1.4.0 release tarball. Make sure you have apr,
#   apr-util, neon, serf, zlib and sqlite subdirectories in your current
#   working directory or specify the path to them with the -apr, -apru,
#   -neon or -zlib options.  For example:
#      ./dist.sh -v 1.4.0 -r ????? -pr branches/1.4.x \
#        -apr  ~/in-tree-libraries/apr-0.9.12 \
#        -apru ~/in-tree-libraries/apr-util-0.9.12 \
#        -neon ~/in-tree-libraries/neon-0.25.5 \
#        -zlib ~/in-tree-libraries/zlib-1.2.3
#
#   Note that there is _no_ need to run dist.sh from a Subversion
#   working copy, so you may wish to create a dist-resources directory
#   containing the apr/, apr-util/, neon/, serf/, zlib/ and sqlite/
#   dependencies, and run dist.sh from that.
#  
#   When building alpha, beta or rc tarballs pass the appropriate flag
#   followed by a number.  For example "-alpha 5", "-beta 3", "-rc 2".
# 
#   If neither an -alpha, -beta, -pre or -rc option is specified, a release
#   tarball will be built.
#  
#   To build a Windows zip file package, additionally pass -zip and the
#   path to apr-iconv with -apri.


USAGE="USAGE: ./dist.sh -v VERSION -r REVISION -pr REPOS-PATH \
[-alpha ALPHA_NUM|-beta BETA_NUM|-rc RC_NUM|-pre PRE_NUM] \
[-apr APR_PATH ] [-apru APR_UTIL_PATH] [-apri APR_ICONV_PATH] \
[-neon NEON_PATH ] [-serf SERF_PATH] [-zlib ZLIB_PATH] \
[-sqlite SQLITE_PATH] [-zip] [-sign] [-nodeps]
 EXAMPLES: ./dist.sh -v 0.36.0 -r 8278 -pr branches/foo
           ./dist.sh -v 0.36.0 -r 8278 -pr trunk
           ./dist.sh -v 0.36.0 -r 8282 -rs 8278 -pr tags/0.36.0
           ./dist.sh -v 0.36.0 -r 8282 -rs 8278 -pr tags/0.36.0 -alpha 1
           ./dist.sh -v 0.36.0 -r 8282 -rs 8278 -pr tags/0.36.0 -beta 1
           ./dist.sh -v 0.36.0 -r 8282 -rs 8278 -pr tags/0.36.0 -pre 1
           ./dist.sh -v 0.36.0 -r 8282 -rs 8278 -pr tags/0.36.0 -nightly r8282"

# Let's check and set all the arguments
ARG_PREV=""

for ARG in $@
do
  if [ -n "$ARG_PREV" ]; then
    case $ARG_PREV in
         -v)  VERSION="$ARG" ;;
         -r)  REVISION="$ARG" ;;
        -pr)  REPOS_PATH="$ARG" ;;
     -alpha)  ALPHA="$ARG" ;;
      -beta)  BETA="$ARG" ;;
       -pre)  PRE="$ARG" ;;
   -nightly)  NIGHTLY="$ARG" ;;
        -rc)  RC="$ARG" ;;
       -apr)  APR_PATH="$ARG" ;;
      -apru)  APRU_PATH="$ARG" ;;
      -apri)  APRI_PATH="$ARG" ;;
      -zlib)  ZLIB_PATH="$ARG" ;;
    -sqlite)  SQLITE_PATH="$ARG" ;;
      -neon)  NEON_PATH="$ARG" ;;
      -serf)  SERF_PATH="$ARG" ;;
    esac
    ARG_PREV=""
  else
    case $ARG in
      -v|-r|-rs|-pr|-alpha|-beta|-pre|-rc|-apr|-apru|-apri|-zlib|-sqlite|-neon|-serf|-nightly)
        ARG_PREV=$ARG
        ;;
      -zip) ZIP=1 ;;
      -nodeps) NODEPS=1 ;;
      -sign) SIGN=1 ;;
      *)
        echo " $USAGE"
        exit 1
        ;;
    esac
  fi
done

if [ -n "$ALPHA" ] && [ -n "$BETA" ] && [ -n "$NIGHTLY" ] && [ -n "$PRE" ] ||
   [ -n "$ALPHA" ] && [ -n "$RC" ] && [ -n "$NIGHTLY" ] && [ -n "$PRE" ] ||
   [ -n "$BETA" ] && [ -n "$RC" ] && [ -n "$NIGHTLY" ] && [ -n "$PRE" ] ||
   [ -n "$ALPHA" ] && [ -n "$BETA" ] && [ -n "$RC" ] && [ -n "$PRE" ] ||
   [ -n "$ALPHA" ] && [ -n "$BETA" ] && [ -n "$RC" ] && [ -n "$PRE" ]; then
  echo " $USAGE"
  exit 1
elif [ -n "$ALPHA" ] ; then
  VER_TAG="Alpha $ALPHA"
  VER_NUMTAG="-alpha$ALPHA" 
elif [ -n "$BETA" ] ; then
  VER_TAG="Beta $BETA"
  VER_NUMTAG="-beta$BETA"
elif [ -n "$RC" ] ; then
  VER_TAG="Release Candidate $RC"
  VER_NUMTAG="-rc$RC"
elif [ -n "$NIGHTLY" ] ; then
  VER_TAG="Nightly Build ($NIGHTLY)"
  VER_NUMTAG="-nightly-$NIGHTLY"
elif [ -n "$PRE" ] ; then
  VER_TAG="Pre-release $PRE"
  VER_NUMTAG="-pre$PRE"
else
  VER_TAG="r$REVISION"
  VER_NUMTAG=""
fi

if [ -n "$ZIP" ] ; then
  EXTRA_EXPORT_OPTIONS="--native-eol CRLF"
fi

if [ -z "$VERSION" ] || [ -z "$REVISION" ] || [ -z "$REPOS_PATH" ]; then
  echo " $USAGE"
  exit 1
fi

if [ -z "$APR_PATH" ]; then
  APR_PATH='apr'
fi

if [ -z "$APRU_PATH" ]; then
  APRU_PATH='apr-util'
fi

if [ -z "$NEON_PATH" ]; then
  NEON_PATH='neon'
fi

if [ -z "$SERF_PATH" ]; then
  SERF_PATH='serf'
fi

if [ -z "$APRI_PATH" ]; then
  APRI_PATH='apr-iconv'
fi

if [ -z "$ZLIB_PATH" ]; then
  ZLIB_PATH='zlib'
fi

if [ -z "$SQLITE_PATH" ]; then
  SQLITE_PATH='sqlite-amalgamation'
fi

REPOS_PATH="`echo $REPOS_PATH | sed 's/^\/*//'`"

# See comment when we 'roll' the tarballs as to why pax is required.
type pax > /dev/null 2>&1
if [ $? -ne 0 ] && [ -z "$ZIP" ]; then
  echo "ERROR: pax could not be found"
  exit 1
fi

# Default to 'wget', but allow 'curl' to be used if available.
HTTP_FETCH=wget
HTTP_FETCH_OUTPUT="-O"
type wget > /dev/null 2>&1
if [ $? -ne 0 ]; then
  type curl > /dev/null 2>&1
  if [ $? -ne 0 ]; then
    echo "Neither curl or wget found."
    exit 2
  fi
  HTTP_FETCH=curl
  HTTP_FETCH_OUTPUT="-o"
fi

DISTNAME="subversion-${VERSION}${VER_NUMTAG}"
DEPSNAME="subversion-deps-${VERSION}${VER_NUMTAG}"
DIST_SANDBOX=.dist_sandbox
DISTPATH="$DIST_SANDBOX/$DISTNAME"
DEPSPATH="$DIST_SANDBOX/deps/$DISTNAME"

echo "Distribution will be named: $DISTNAME"
echo "     constructed from path: /$REPOS_PATH"
echo " constructed from revision: $REVISION"

rm -rf "$DIST_SANDBOX"
mkdir "$DIST_SANDBOX"
mkdir -p "$DEPSPATH"
echo "Removed and recreated $DIST_SANDBOX"

LC_ALL=C
LANG=C
TZ=UTC
export LC_ALL
export LANG
export TZ

echo "Exporting $REPOS_PATH r$REVISION into sandbox..."
(cd "$DIST_SANDBOX" && \
 ${SVN:-svn} export -q $EXTRA_EXPORT_OPTIONS -r "$REVISION" \
     "http://svn.collab.net/repos/svn/$REPOS_PATH" \
     "$DISTNAME" --username none --password none)

rm -f "$DISTPATH/STATUS"

# Remove the www/ directory, and create an empty directory in it's place.
# Export hacking.html from trunk into that directory.
# (See http://svn.haxx.se/dev/archive-2008-02/0863.shtml for rationale.)
rm -rf "$DISTPATH/www"
mkdir "$DISTPATH/www"
${SVN:-svn} export -q $EXTRA_EXPORT_OPTIONS -r "$REVISION" \
    "http://svn.collab.net/repos/svn/trunk/www/hacking.html" \
    --username none --password none "$DISTPATH/www/hacking.html"

install_dependency()
{
  DEP_NAME=$1
  if [ -z $2 ]; then
    DEP_PATH=/dev/null
  else
    DEP_PATH=$2
  fi

  if [ -d $DEP_PATH ]; then
    if [ -d $DEP_PATH/.svn ]; then
      echo "Exporting local $DEP_NAME into sandbox"
      ${SVN:-svn} export -q $EXTRA_EXPORT_OPTIONS "$DEP_PATH" "$DISTPATH/$DEP_NAME"
    else
      echo "Copying local $DEP_NAME into sandbox"
      cp -r "$DEP_PATH" "$DISTPATH/$DEP_NAME" 
      (cd "$DISTPATH/$DEP_NAME" && [ -f Makefile ] && make distclean)
      echo "Removing all CVS/ and .cvsignore files from $DEP_NAME..."
      find "$DISTPATH/$DEP_NAME" -name CVS -type d -print | xargs rm -fr
      find "$DISTPATH/$DEP_NAME" -name .cvsignore -print | xargs rm -f
      find "$DISTPATH/$DEP_NAME" -name '*.o' -print | xargs rm -f
    fi
  else
    # Not having the dependency directories isn't fatal if -nodeps passed.
    if [ -z "$NODEPS" ]; then
      echo "Missing dependency directory!"
      exit 2
    fi
  fi
}

move_dependency()
{
  DEP_NAME=$1

  SOURCE_PATH="$DISTPATH/$DEP_NAME"
  DEST_PATH="$DEPSPATH/$DEP_NAME"

  rm -rf "$DEST_PATH"
  mv "$SOURCE_PATH" "$DEST_PATH"
}

install_dependency apr "$APR_PATH"
install_dependency apr-util "$APRU_PATH"

if [ -n "$ZIP" ]; then
  install_dependency apr-iconv "$APRI_PATH"
fi

install_dependency neon "$NEON_PATH"
install_dependency serf "$SERF_PATH"
install_dependency zlib "$ZLIB_PATH"
install_dependency sqlite "$SQLITE_PATH"


find "$DISTPATH" -name config.nice -print | xargs rm -f

# Massage the new version number into svn_version.h.  We need to do
# this before running autogen.sh --release on the subversion code,
# because otherwise svn_version.h's mtime makes SWIG files regenerate
# on end-user's systems, when they should just be compiled by the
# Release Manager and left at that.

ver_major=`echo $VERSION | cut -d '.' -f 1`
ver_minor=`echo $VERSION | cut -d '.' -f 2`
ver_patch=`echo $VERSION | cut -d '.' -f 3`

vsn_file="$DISTPATH/subversion/include/svn_version.h"

sed \
 -e "/#define *SVN_VER_MAJOR/s/[0-9]\+/$ver_major/" \
 -e "/#define *SVN_VER_MINOR/s/[0-9]\+/$ver_minor/" \
 -e "/#define *SVN_VER_PATCH/s/[0-9]\+/$ver_patch/" \
 -e "/#define *SVN_VER_TAG/s/\".*\"/\" ($VER_TAG)\"/" \
 -e "/#define *SVN_VER_NUMTAG/s/\".*\"/\"$VER_NUMTAG\"/" \
 -e "/#define *SVN_VER_REVISION/s/[0-9]\+/$REVISION/" \
  < "$vsn_file" > "$vsn_file.tmp"

mv -f "$vsn_file.tmp" "$vsn_file"

echo "Creating svn_version.h.dist, for use in tagging matching tarball..."
cp "$vsn_file" "svn_version.h.dist"

# Don't run autogen.sh when we are building the Windows zip file.
# Windows users don't need the files generated by this command,
# especially not the generated projects or SWIG files.
if [ -z "$ZIP" ] ; then
  echo "Running ./autogen.sh in sandbox, to create ./configure ..."
  (cd "$DISTPATH" && ./autogen.sh --release) || exit 1
fi

echo "Removing any autom4te.cache directories that might exist..."
find "$DISTPATH" -depth -type d -name 'autom4te*.cache' -exec rm -rf {} \;

# Now that the dependencies have been configured/cleaned properly,
# move them into their separate tree for packaging.
move_dependency apr
move_dependency apr-util
if [ -n "$ZIP" ]; then
  move_dependency apr-iconv
fi
move_dependency neon
move_dependency serf
move_dependency zlib
move_dependency sqlite

if [ -z "$ZIP" ]; then
  # Do not use tar, it's probably GNU tar which produces tar files that are
  # not compliant with POSIX.1 when including filenames longer than 100 chars.
  # Platforms without a tar that understands the GNU tar extension will not
  # be able to extract the resulting tar file.  Use pax to produce POSIX.1
  # tar files.
  echo "Rolling $DISTNAME.tar ..."
  (cd "$DIST_SANDBOX" > /dev/null && pax -x ustar -w "$DISTNAME") > \
    "$DISTNAME.tar"
  echo "Rolling $DEPSNAME.tar ..."
  (cd "$DIST_SANDBOX/deps" > /dev/null && pax -x ustar -w "$DISTNAME") > \
    "$DEPSNAME.tar"

  echo "Compressing to $DISTNAME.tar.bz2 ..."
  bzip2 -9fk "$DISTNAME.tar"
  echo "Compressing to $DEPSNAME.tar.bz2 ..."
  bzip2 -9fk "$DEPSNAME.tar"

  # Use the gzip -n flag - this prevents it from storing the original name of
  # the .tar file, and far more importantly, the mtime of the .tar file, in the
  # produced .tar.gz file. This is important, because it makes the gzip
  # encoding reproducable by anyone else who has an similar version of gzip,
  # and also uses "gzip -9n". This means that committers who want to GPG-sign
  # both the .tar.gz and the .tar.bz2 can download the .tar.bz2 (which is
  # smaller), and locally generate an exact duplicate of the official .tar.gz
  # file. This metadata is data on the temporary uncompressed tarball itself,
  # not any of its contents, so there will be no effect on end-users.
  echo "Compressing to $DISTNAME.tar.gz ..."
  gzip -9nf "$DISTNAME.tar"
  echo "Compressing to $DEPSNAME.tar.gz ..."
  gzip -9nf "$DEPSNAME.tar"
else
  echo "Rolling $DISTNAME.zip ..."
  (cd "$DIST_SANDBOX" > /dev/null && zip -q -r - "$DISTNAME") > \
    "$DISTNAME.zip"
  echo "Rolling $DEPSNAME.zip ..."
  (cd "$DIST_SANDBOX/deps" > /dev/null && zip -q -r - "$DISTNAME") > \
    "$DEPSNAME.zip"
fi
echo "Removing sandbox..."
rm -rf "$DIST_SANDBOX"

sign_file()
{
  if [ -n "$SIGN" ]; then
    type gpg > /dev/null 2>&1
    if [ $? -eq 0 ]; then
      if test -n "$user"; then
        args="--default-key $user"
      fi
      for ARG in $@
      do
        gpg --armor $args --detach-sign $ARG
      done
    else
      type pgp > /dev/null 2>&1
      if [ $? -eq 0 ]; then
        if test -n "$user"; then
          args="-u $user"
        fi
        for ARG in $@
        do
          pgp -sba $ARG $args
        done
      fi
    fi
  fi
}

echo ""
echo "Done:"
if [ -z "$ZIP" ]; then
  ls -l "$DISTNAME.tar.bz2" "$DISTNAME.tar.gz" "$DEPSNAME.tar.bz2" "$DEPSNAME.tar.gz"
  sign_file $DISTNAME.tar.gz $DISTNAME.tar.bz2 $DEPSNAME.tar.bz2 $DEPSNAME.tar.gz
  echo ""
  echo "md5sums:"
  md5sum "$DISTNAME.tar.bz2" "$DISTNAME.tar.gz" "$DEPSNAME.tar.bz2" "$DEPSNAME.tar.gz"
  type sha1sum > /dev/null 2>&1
  if [ $? -eq 0 ]; then
    echo ""
    echo "sha1sums:"
    sha1sum "$DISTNAME.tar.bz2" "$DISTNAME.tar.gz" "$DEPSNAME.tar.bz2" "$DEPSNAME.tar.gz"
  fi
else
  ls -l "$DISTNAME.zip" "$DEPSNAME.zip"
  sign_file $DISTNAME.zip $DEPSNAME.zip
  echo ""
  echo "md5sum:"
  md5sum "$DISTNAME.zip" "$DEPSNAME.zip"
  type sha1sum > /dev/null 2>&1
  if [ $? -eq 0 ]; then
    echo ""
    echo "sha1sum:"
    sha1sum "$DISTNAME.zip" "$DEPSNAME.zip"
  fi
fi
