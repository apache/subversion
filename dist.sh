#!/bin/sh

#
# USAGE: ./dist.sh VERSION REVISION [REPOS-PATH]
#
#   Create a distribution tarball, labelling it with the given VERSION.
#   The REVISION will be used in the version string. The tarball will be
#   constructed from the root located at REPOS-PATH. If REPOS-PATH is
#   not specified then the default is "branches/release-VERSION". For
#   example, the command line:
#
#      ./dist.sh 0.24.2 6284
#
#   from the top-level of a branches/release-0.24.2 working copy will
#   create the 0.24.2 release tarball. Make sure you have apr, apr-util,
#   and neon subdirectories and that the working copy is configured
#   before running this script in the top-level directory.
#

if [ -z "$1" ] || [ -z "$2" ]; then
  echo "USAGE: ./dist.sh VERSION REVISION [REPOS-PATH]"
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

VERSION="$1"

REVISION="$2"
WC_REVISION="`svnversion doc`"

REPOS_PATH="$3"
if [ -z "$REPOS_PATH" ]; then
  REPOS_PATH="branches/release-$VERSION"
else
  REPOS_PATH="`echo $REPOS_PATH | sed 's/^\/*//'`"
fi

DISTNAME="subversion-$VERSION"
DIST_SANDBOX=.dist_sandbox
DISTPATH="$DIST_SANDBOX/$DISTNAME"

echo "Distribution will be named: $DISTNAME"
echo "     constructed from path: /$REPOS_PATH"

if [ "$WC_REVISION" != "$REVISION" ]; then
  echo "* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *"
  echo "*                                                             *"
  echo "* WARNING:  The docs/ directory in your working copy does not *"
  echo "*           appear  to  have the same revision number  as the *"
  echo "*           distribution revision you requested.  Since these *"
  echo "*           documents will be the ones included in your final *"
  echo "*           tarball, please  be  sure they reflect the proper *"
  echo "*           state.                                            *"
  echo "*                                                             *"
  echo "* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *"
fi

echo "Cleaning old docs in docs/ ..."
make doc-clean
rm -f doc/translations/french/svn-handbook-french.info
rm -f doc/translations/french/svn-handbook-french.info-*
rm -f doc/translations/french/svn-handbook-french.html
rm -f doc/translations/french/svn-handbook-french.txt

echo "Building new docs in docs/ ..."
FOP_OPTS="-Xms100m -Xmx200m"
export FOP_OPTS
make doc

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

echo "Copying new docs into sandbox..."
for name in doc/programmer/design/svn-design.info   \
            doc/programmer/design/svn-design.info-* \
            doc/programmer/design/svn-design.html   \
            doc/programmer/design/svn-design.txt    \
            doc/book/book/*.html                    \
            doc/book/book/*.pdf                     \
            doc/book/book/*.ps
do
   cp "$name" "$DISTPATH/$name"
done

cat > "$DISTPATH/ChangeLog.CVS" <<EOF
The old CVS ChangeLog is kept at 

     http://subversion.tigris.org/

If you want to see changes since Subversion went self-hosting,
you probably want to use the "svn log" command -- and if it 
does not do what you need, please send in a patch!
EOF

vsn_file="$DISTPATH/subversion/include/svn_version.h"

sed -e \
 "/#define *SVN_VER_TAG/s/dev build/r$REVISION/" \
  < "$vsn_file" > "$vsn_file.tmp"

sed -e \
 "/#define *SVN_VER_NUMTAG/s/\+//" \
  < "$vsn_file.tmp" > "$vsn_file.unq"

sed -e \
 "/#define *SVN_VER_REVISION/s/0/$REVISION/" \
  < "$vsn_file.unq" > "$vsn_file"

rm -f "$vsn_file.tmp"
rm -f "$vsn_file.unq"

echo "Rolling $DISTNAME.tar.gz ..."
(cd "$DIST_SANDBOX" && tar zcpf "$DISTNAME.tar.gz" "$DISTNAME")

echo "Copying tarball out, removing sandbox..."
cp "$DISTPATH.tar.gz" .
rm -rf "$DIST_SANDBOX"

echo ""
echo "Done:"
ls -l "$DISTNAME.tar.gz"
echo ""

