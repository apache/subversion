#!/bin/sh

#
# USAGE: ./dist.sh -v VERSION -r REVISION [-rs REVISION-SVN] [-pr REPOS-PATH]
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
#   and neon subdirectories and that the working copy is configured
#   before running this script in the top-level directory.
#

# A quick and dirty usage message
USAGE="USAGE: ./dist.sh -v VERSION -r REVISION \
[-rs REVISION-SVN ] [-pr REPOS-PATH]
 EXAMPLES: ./dist.sh -v 0.36.0 -r 8278
           ./dist.sh -v 0.36.0 -r 8278 -pr trunk
           ./dist.sh -v 0.36.0 -r 8282 -rs 8278 -pr tags/0.36.0"

# Let's check and set all the arguments
ARG_PREV=""

for ARG in $@
do
  if [ "$ARG_PREV" ]; then

    case $ARG_PREV in
       -v) VERSION="$ARG" ;;
       -r) REVISION="$ARG" ;;
      -rs) REVISION_SVN="$ARG" ;;
      -pr) REPOS_PATH="$ARG" ;;
        *) ARG_PREV=$ARG ;;
    esac

    ARG_PREV=""

  else

    case $ARG in
      -v|-r|-rs|-pr)
        ARG_PREV=$ARG
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

if [ -z "$VERSION" ] || [ -z "$REVISION" ] ; then
  echo " $USAGE"
  exit 1
fi

if [ ! -d apr ]; then
  echo "ERROR: an 'apr' subdirectory must be present."
  exit 1
fi

if [ ! -d apr-util ]; then
  echo "ERROR: an 'apr-util' subdirectory must be present."
  exit 1
fi

if [ ! -d neon ]; then
  echo "ERROR: a 'neon' subdirectory must be present."
  exit 1
fi

if [ -z "$REPOS_PATH" ]; then
  REPOS_PATH="branches/$VERSION"
else
  REPOS_PATH="`echo $REPOS_PATH | sed 's/^\/*//'`"
fi

DISTNAME="subversion-$VERSION"
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
 svn export -q -r "$REVISION" "http://svn.collab.net/repos/svn/$REPOS_PATH" \
     "$DISTNAME" --username none --password none)

for pkg in apr-util apr ; do
  echo "Copying $pkg into sandbox, making extraclean..."
  cp -r "$pkg" "$DISTPATH"
  (cd "$DISTPATH/$pkg" && make extraclean)

  echo "Removing all CVS/ and .cvsignore files from $pkg..."
  find "$DISTPATH/$pkg" -name CVS -type d -print | xargs rm -fr
  find "$DISTPATH/$pkg" -name .cvsignore -print | xargs rm -f
done

echo "Coping neon into sandbox, making clean..."
cp -r neon "$DISTPATH"
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
 -e "/#define *SVN_VER_TAG/s/dev build/r$REVISION_SVN/" \
 -e '/#define *SVN_VER_NUMTAG/s/".*"/""/' \
 -e "/#define *SVN_VER_REVISION/s/0/$REVISION_SVN/" \
  < "$vsn_file" > "$vsn_file.tmp"

mv -f "$vsn_file.tmp" "$vsn_file"

cp "$vsn_file" "svn_version.h.dist"

# Do not use tar, it's probably GNU tar which produces tar files that are
# not compliant with POSIX.1 when including filenames longer than 100 chars.
# Platforms without a tar that understands the GNU tar extension will not
# be able to extract the resulting tar file.  Use pax to produce POSIX.1
# tar files.
echo "Rolling $DISTNAME.tar.gz ..."
(cd "$DIST_SANDBOX" > /dev/null && pax -x ustar -w "$DISTNAME") | \
  gzip -9c > "$DISTNAME.tar.gz"
echo "Rolling $DISTNAME.tar.bz2 ..."
(cd "$DIST_SANDBOX" > /dev/null && pax -x ustar -w "$DISTNAME") | \
  bzip2 -9c > "$DISTNAME.tar.bz2"

echo "Removing sandbox..."
rm -rf "$DIST_SANDBOX"

echo ""
echo "Done:"
ls -l "$DISTNAME.tar.gz"
ls -l "$DISTNAME.tar.bz2"
echo ""

