#!/bin/sh

#
# USAGE: ./dist.sh -v VERSION -r REVISION [-rs REVISION-SVN] [-pr REPOS-PATH]
#                  [-apr PATH-TO-APR ] [-apu PATH-TO-APR-UTIL] 
#                  [-neon PATH-TO-NEON ] [-alpha|-beta BETA_NUM|-rc RC_NUM]
#
#   Create a distribution tarball, labelling it with the given VERSION.
#   The REVISION or REVISION-SVN will be used in the version string.
#   The tarball will be constructed from the root located at REPOS-PATH.
#   If REPOS-PATH is not specified then the default is "branches/VERSION".
#   For example, the command line:
#
#      ./dist.sh -v 0.24.2 -r 6284
#
#   from the top-level of a branches/0.24.2 working copy will create
#   the 0.24.2 release tarball. Make sure you have apr, apr-util,
#   and neon subdirectories in your current working directory or
#   specify the path to them with the -apr, -apu or -neon options.
#   For example:
#      ./dist.sh -v 1.1.0 -r 10277 -pr branches/1.1.x \
#        -apr -neon ~/in-tree-libraries/neon-0.24.7 \
#        -apr ~/in-tree-libraries/httpd-2.0.50/srclib/apr \
#        -apu ~/in-tree-libraries/httpd-2.0.50/srclib/apr-util/
#
#   When building a beta tarball pass -beta NUM where num is the beta
#   number.  For example:
#      ./dist.sh -v 1.1.0 -r 10277 -pr branches/1.1.x -beta 1
# 
#   Alpha versions can be specified with just -alpha but take no
#   number parameter since they are not intended for public release.
#   For example:
#      ./dist.sh -v 1.1.0 -r 10277 -pr branches/1.1.x -alpha
#
#   If neither an -beta or -rc option with a number or an -alpha option
#   are specified, it will build a release tarball.


# A quick and dirty usage message
USAGE="USAGE: ./dist.sh -v VERSION -r REVISION \
[-rs REVISION-SVN ] [-pr REPOS-PATH] \
[-alpha|-beta BETA_NUM|-rc RC_NUM] [-apr APR_PATH ] \
[-apu APR_UTIL_PATH] [-neon NEON_PATH ]
 EXAMPLES: ./dist.sh -v 0.36.0 -r 8278
           ./dist.sh -v 0.36.0 -r 8278 -pr trunk
           ./dist.sh -v 0.36.0 -r 8282 -rs 8278 -pr tags/0.36.0
           ./dist.sh -v 0.36.0 -r 8282 -rs 8278 -pr tags/0.36.0 -alpha
           ./dist.sh -v 0.36.0 -r 8282 -rs 8278 -pr tags/0.36.0 -beta 1"

# Let's check and set all the arguments
ARG_PREV=""

for ARG in $@
do
  if [ "$ARG_PREV" ]; then

    case $ARG_PREV in
         -v)  VERSION="$ARG" ;;
         -r)  REVISION="$ARG" ;;
        -rs)  REVISION_SVN="$ARG" ;;
        -pr)  REPOS_PATH="$ARG" ;;
	-rc)  RC="$ARG" ;;
       -apr)  APR_PATH="$ARG" ;;
       -apu)  APU_PATH="$ARG" ;;
      -neon)  NEON_PATH="$ARG" ;;
      -beta)  BETA="$ARG" ;;
          *)  ARG_PREV=$ARG ;;
    esac

    ARG_PREV=""

  else

    case $ARG in
      -v|-r|-rs|-pr|-beta|-apr|-apu|-neon)
        ARG_PREV=$ARG
        ;;
      -alpha)
        ALPHA="1"
        ARG_PREV=""
        ;;
      *)
        echo " $USAGE"
        exit 1
        ;;
    esac
  fi
done

if [ -z "$REVISION_SVN" ]; then
  REVISION_SVN=$REVISION
fi

if [ -n "$ALPHA" ] && [ -n "$BETA" ] ||
   [ -n "$ALPHA" ] && [ -n "$RC" ] ||
   [ -n "$BETA" ] && [ -n "$RC" ] ; then
  echo " $USAGE"
  exit 1
elif [ -n "$ALPHA" ] ; then
  VER_TAG="Alpha"
  VER_NUMTAG="-alpha" 
elif [ -n "$BETA" ] ; then
  VER_TAG="Beta $BETA"
  VER_NUMTAG="-beta$BETA"
elif [ -n "$RC" ] ; then
  VER_TAG="Release Candidate $RC"
  VER_NUMTAG="-rc$RC"
else
  VER_TAG="r$REVISION_SVN"
  VER_NUMTAG=""
fi
  
if [ -z "$VERSION" ] || [ -z "$REVISION" ] ; then
  echo " $USAGE"
  exit 1
fi

if [ -z "$APR_PATH" ]; then
  APR_PATH='apr'
fi

if [ -z "$APU_PATH" ]; then
  APU_PATH='apu'
fi

if [ -z "$NEON_PATH" ]; then
  NEON_PATH='neon'
fi

if [ ! -d "$APR_PATH" ]; then
  echo "ERROR: '$APR_PATH' does not exist."
  exit 1
fi

if [ ! -d "$APU_PATH" ]; then
  echo "ERROR: '$APU_PATH' does not exist."
  exit 1
fi

if [ ! -d "$NEON_PATH" ]; then
  echo "ERROR: '$NEON_PATH' does not exist."
  exit 1
fi

if [ -z "$REPOS_PATH" ]; then
  REPOS_PATH="branches/$VERSION"
else
  REPOS_PATH="`echo $REPOS_PATH | sed 's/^\/*//'`"
fi

DISTNAME="subversion-${VERSION}${VER_NUMTAG}"
DIST_SANDBOX=.dist_sandbox
DISTPATH="$DIST_SANDBOX/$DISTNAME"

echo "Distribution will be named: $DISTNAME"
echo " release branch's revision: $REVISION"
echo "     executable's revision: $REVISION_SVN"
echo "     constructed from path: /$REPOS_PATH"

rm -rf "$DIST_SANDBOX"
mkdir "$DIST_SANDBOX"
echo "Removed and recreated $DIST_SANDBOX"

echo "Exporting revision $REVISION of Subversion into sandbox..."
(cd "$DIST_SANDBOX" && \
 ${SVN:-svn} export -q -r "$REVISION" \
     "http://svn.collab.net/repos/svn/$REPOS_PATH" \
     "$DISTNAME" --username none --password none)

echo "Copying $APR_PATH into sandbox, making extraclean..."
cp -r "$APR_PATH" "$DISTPATH/apr"
(cd "$DISTPATH/apr" && make extraclean)
echo "Removing all CVS/ and .cvsignore files from apr..."
find "$DISTPATH/apr" -name CVS -type d -print | xargs rm -fr
find "$DISTPATH/apr" -name .cvsignore -print | xargs rm -f

echo "Copying $APU_PATH into sandbox, making extraclean..."
cp -r "$APU_PATH" "$DISTPATH/apr-util"
(cd "$DISTPATH/apr-util" && make extraclean)
echo "Removing all CVS/ and .cvsignore files from apr-util..."
find "$DISTPATH/apr-util" -name CVS -type d -print | xargs rm -fr
find "$DISTPATH/apr-util" -name .cvsignore -print | xargs rm -f

echo "Coping neon into sandbox, making clean..."
cp -r "$NEON_PATH" "$DISTPATH/neon"
(cd "$DISTPATH/neon" && make distclean)
echo "Cleaning *.o in neon..."
find "$DISTPATH/neon/src" -name '*.o' -print | xargs rm -f

find "$DISTPATH" -name config.nice -print | xargs rm -f

echo "Running ./autogen.sh in sandbox, to create ./configure ..."
(cd "$DISTPATH" && ./autogen.sh --release) || exit 1

echo "Downloading book into sandbox..."

wget http://svnbook.red-bean.com/book.pdf \
  -O "$DISTPATH/doc/book/book/book.pdf" ||
  ( echo "ERROR: Problem getting the book.pdf file." && exit 1 )

wget http://svnbook.red-bean.com/book.html \
  -O "$DISTPATH/doc/book/book/book.html" ||
  ( echo "ERROR: Problem getting the book.html file." && exit 1 )

cat > "$DISTPATH/ChangeLog.CVS" <<EOF
The old CVS ChangeLog is kept at 

     http://subversion.tigris.org/

If you want to see changes since Subversion went self-hosting,
you probably want to use the "svn log" command -- and if it 
does not do what you need, please send in a patch!
EOF

ver_major=`echo $VERSION | cut -d '.' -f 1`
ver_minor=`echo $VERSION | cut -d '.' -f 2`
ver_patch=`echo $VERSION | cut -d '.' -f 3`

vsn_file="$DISTPATH/subversion/include/svn_version.h"

sed \
 -e "/#define *SVN_VER_MAJOR/s/[0-9]\+/$ver_major/" \
 -e "/#define *SVN_VER_MINOR/s/[0-9]\+/$ver_minor/" \
 -e "/#define *SVN_VER_PATCH/s/[0-9]\+/$ver_patch/" \
 -e "/#define *SVN_VER_MICRO/s/[0-9]\+/$ver_patch/" \
 -e "/#define *SVN_VER_TAG/s/\".*\"/\" ($VER_TAG)\"/" \
 -e "/#define *SVN_VER_NUMTAG/s/\".*\"/\"$VER_NUMTAG\"/" \
 -e "/#define *SVN_VER_REVISION/s/[0-9]\+/$REVISION_SVN/" \
  < "$vsn_file" > "$vsn_file.tmp"

mv -f "$vsn_file.tmp" "$vsn_file"

cp "$vsn_file" "svn_version.h.dist"

# Do not use tar, it's probably GNU tar which produces tar files that are
# not compliant with POSIX.1 when including filenames longer than 100 chars.
# Platforms without a tar that understands the GNU tar extension will not
# be able to extract the resulting tar file.  Use pax to produce POSIX.1
# tar files.
echo "Rolling $DISTNAME.tar ..."
(cd "$DIST_SANDBOX" > /dev/null && pax -x ustar -w "$DISTNAME") > \
  "$DISTNAME.tar"

echo "Compressing to $DISTNAME.tar.bz2 ..."
bzip2 -9k "$DISTNAME.tar"

echo "Compressing to $DISTNAME.tar.gz ..."
gzip -9 "$DISTNAME.tar"

echo "Removing sandbox..."
rm -rf "$DIST_SANDBOX"

echo ""
echo "Done:"
ls -l "$DISTNAME.tar.gz" "$DISTNAME.tar.bz2"
echo ""
md5sum "$DISTNAME.tar.gz" "$DISTNAME.tar.bz2"

