#!/bin/sh

#
# USAGE: ./dist.sh -v VERSION -r REVISION [-rs REVISION-SVN] [-pr REPOS-PATH]
#                  [-apr APR-PATH] [-apu APR-UTIL-PATH] 
#                  [-api APR-ICONV-PATH] [-neon NEON-PATH]
#                  [-apr-tag APR-TAG] [-apu-tag APR-UTIL-TAG] 
#                  [-api-tag APR-ICONV-TAG] [-neon-tag NEON-TAG]
#                  [-zip] [-sign] [-fetch]
#                  [-alpha ALPHA_NUM|-beta BETA_NUM|-rc RC_NUM]
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
#   specify the path to them with the -apr, -apru or -neon options.
#   For example:
#      ./dist.sh -v 1.1.0 -r 10277 -pr branches/1.1.x \
#        -neon ~/in-tree-libraries/neon-0.24.7 \
#        -apr ~/in-tree-libraries/httpd-2.0.50/srclib/apr \
#        -apu ~/in-tree-libraries/httpd-2.0.50/srclib/apr-util/
#
#   When building a alpha, beta or rc tarballs pass the apppropriate flag
#   followeb by the number for that releasse.  For example you'd do
#   the following for a Beta 1 release:
#      ./dist.sh -v 1.1.0 -r 10277 -pr branches/1.1.x -beta 1
# 
#   If neither an -alpha, -beta or -rc option with a number is
#   specified, it will build a release tarball.
#  
#   To build a Windows zip file package pass -zip and the path
#   to apr-iconv with -apri.


# A quick and dirty usage message
USAGE="USAGE: ./dist.sh -v VERSION -r REVISION
[-rs REVISION-SVN] [-pr REPOS-PATH]
[-alpha ALPHA_NUM|-beta BETA_NUM|-rc RC_NUM]
[-apr APR_DIR] [-apu APR_UTIL_DIR] [-api APR_ICONV_DIR] [-neon NEON_DIR]
[-apr-tag APR_TAG] [-apu-tag APR_UTIL_TAG] [-api-tag APR_ICONV_TAG]
[-neon-tag NEON_TAG]
[-zip] [-sign] [-fetch]
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
       -api)  API_PATH="$ARG" ;;
   -apr-tag)  APR_TAG="$ARG" ;;
   -apu-tag)  APU_TAG="$ARG" ;;
   -api-tag)  API_TAG="$ARG" ;;
      -neon)  NEON_PATH="$ARG" ;;
  -neon-tag)  NEON_TAG="$ARG" ;;
      -beta)  BETA="$ARG" ;;
     -alpha)  ALPHA="$ARG" ;;
          *)  ARG_PREV=$ARG ;;
    esac

    ARG_PREV=""

  else

    case $ARG in
      -v|-r|-rs|-pr|-beta|-rc|-alpha|-apr|-apr-tag|-apu|-apu-tag|-api|-api-tag|-neon|-neon-tag)
        ARG_PREV=$ARG
        ;;
      -fetch)
        FETCH=1
        ARG_PREV=""
        ;;
      -sign)
        SIGN=1
        ARG_PREV=""
        ;;
      -zip)
        ZIP=1
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
  VER_TAG="Alpha $ALPHA"
  VER_NUMTAG="-alpha$ALPHA" 
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
  
if [ -n "$ZIP" ] ; then
  EXTRA_EXPORT_OPTIONS="--native-eol CRLF"
fi

if [ -z "$VERSION" ] || [ -z "$REVISION" ] ; then
  echo " $USAGE"
  exit 1
fi

if [ -n "$FETCH" ]; then
  if [ -z "$APR_PATH" -a -z "$APR_TAG" ]; then
    APR_TAG='0.9.6'
  fi
  if [ -z "$APU_PATH" -a -z "$APU_TAG" ]; then
    APU_TAG='0.9.6'
  fi
  if [ -z "$API_PATH" -a -z "$API_PATH" ]; then
    API_TAG='0.9.6'
  fi
  if [ -z "$NEON_PATH" -a -z "$NEON_TAG" ]; then
    NEON_TAG='0.24.7'
  fi
else
  if [ -z "$APR_PATH" -a -z "$APR_TAG" ]; then
    APR_PATH='apr'
  fi
  if [ -z "$APU_PATH" -a -z "$APU_TAG" ]; then
    APU_PATH='apr-util'
  fi
  if [ -z "$API_PATH" -a -z "$API_PATH" ]; then
    API_PATH='apr-iconv'
  fi
  if [ -z "$NEON_PATH" -a -z "$NEON_TAG" ]; then
    NEON_PATH='neon'
  fi

fi

if [ -z "$APR_TAG" -a ! -d "$APR_PATH" ]; then
  echo "Missing $APR_PATH directory.  Aborting."
  echo " $USAGE"
  exit 2
fi
if [ -z "$APU_TAG" -a ! -d "$APU_PATH" ]; then
  echo "Missing $APU_PATH directory.  Aborting."
  echo " $USAGE"
  exit 2
fi
if [ -n "$ZIP" -a -z "$API_TAG" -a ! -d "$API_PATH" ]; then
  echo "Missing $API_PATH directory.  Aborting."
  echo " $USAGE"
  exit 2
fi
if [ -z "$NEON_TAG" -a ! -d "$NEON_PATH" ]; then
  echo "Missing $NEON_PATH directory.  Aborting."
  echo " $USAGE"
  exit 2
fi

if [ -z "$REPOS_PATH" ]; then
  REPOS_PATH="branches/$VERSION"
else
  REPOS_PATH="`echo $REPOS_PATH | sed 's/^\/*//'`"
fi

type pax > /dev/null 2>&1
if [ $? -ne 0 ] && [ -z "$ZIP" ]; then
  echo "ERROR: pax could not be found"
  exit 1
fi

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
 ${SVN:-svn} export -q $EXTRA_EXPORT_OPTIONS -r "$REVISION" \
     "http://svn.collab.net/repos/svn/$REPOS_PATH" \
     "$DISTNAME" --username none --password none)

install_dependency()
{
  DEP_NAME=$1
  DEP_TAG=$2
  if [ -z $3 ]; then
    DEP_PATH=/dev/null
  else
    DEP_PATH=$3
  fi
  if [ -d $DEP_PATH ]; then
    if [ -d $DEP_PATH/.svn ]; then
      echo "Exporting local $DEP_NAME into sandbox"
      ${SVN:-svn} export -q $EXTRA_EXPORT_OPTIONS "$DEP_PATH" "$DISTPATH/$DEP_NAME"
    else
      echo "Copying local $DEP_NAME into sandbox"
      cp -r "$DEP_PATH" "$DISTPATH/$DEP_NAME" 
      (cd "$DISTPATH/$DEP_NAME" && [ -f Makefile ] && make extraclean)
      echo "Removing all CVS/ and .cvsignore files from $DEP_NAME..."
      find "$DISTPATH/$DEP_NAME" -name CVS -type d -print | xargs rm -fr
      find "$DISTPATH/$DEP_NAME" -name .cvsignore -print | xargs rm -f
    fi
  else
    echo "Exporting $DEP_NAME into sandbox ($DEP_TAG)"
    ${SVN:-svn} export -q $EXTRA_EXPORT_OPTIONS $DEP_TAG "$DISTPATH/$DEP_NAME"
  fi
}

install_dependency apr http://svn.apache.org/repos/asf/apr/apr/tags/${APR_TAG} "$APR_PATH"
install_dependency apr-util http://svn.apache.org/repos/asf/apr/apr-util/tags/${APU_TAG} "$APU_PATH"

if [ -n "$ZIP" ]; then
  install_dependency apr-iconv http://svn.apache.org/repos/asf/apr/apr-iconv/tags/${API_TAG} "$API_PATH"
fi

install_dependency neon http://svn.webdav.org/repos/projects/neon/tags/${NEON_TAG} "$NEON_PATH"

find "$DISTPATH" -name config.nice -print | xargs rm -f

echo "Running ./autogen.sh in sandbox, to create ./configure ..."
(cd "$DISTPATH" && ./autogen.sh --release) || exit 1

if [ ! -f $DISTPATH/neon/configure ]; then
  echo "Creating neon configure"
  (cd "$DISTPATH/neon" && ./autogen.sh) || exit 1
fi

echo "Removing any autom4te.cache directories that might exist..."
find "$DISTPATH" -depth -type d -name 'autom4te*.cache' -exec rm -rf {} \;

echo "Downloading book into sandbox..."

BOOK_PDF=http://svnbook.red-bean.com/en/1.1/svn-book.pdf
BOOK_PDF_DEST="$DISTPATH/doc/book/book/svn-book.pdf"
BOOK_HTML=http://svnbook.red-bean.com/en/1.1/svn-book.html
BOOK_HTML_DEST="$DISTPATH/doc/book/book/svn-book.html"

$HTTP_FETCH $BOOK_PDF $HTTP_FETCH_OUTPUT $BOOK_PDF_DEST ||
  ( echo "ERROR: Problem getting the svn-book.pdf file." && exit 1 )

$HTTP_FETCH $BOOK_HTML $HTTP_FETCH_OUTPUT $BOOK_HTML_DEST ||
  ( echo "ERROR: Problem getting the svn-book.html file." && exit 1 )

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

if [ -z "$ZIP" ]; then
  # Do not use tar, it's probably GNU tar which produces tar files that are
  # not compliant with POSIX.1 when including filenames longer than 100 chars.
  # Platforms without a tar that understands the GNU tar extension will not
  # be able to extract the resulting tar file.  Use pax to produce POSIX.1
  # tar files.
  echo "Rolling $DISTNAME.tar ..."
  (cd "$DIST_SANDBOX" > /dev/null && pax -x ustar -w "$DISTNAME") > \
    "$DISTNAME.tar"

  echo "Compressing to $DISTNAME.tar.bz2 ..."
  bzip2 -9fk "$DISTNAME.tar"

  echo "Compressing to $DISTNAME.tar.gz ..."
  gzip -9f "$DISTNAME.tar"
else
  echo "Rolling $DISTNAME.zip ..."
  (cd "$DIST_SANDBOX" > /dev/null && zip -q -r - "$DISTNAME") > \
    "$DISTNAME.zip"
fi
#echo "Removing sandbox..."
#rm -rf "$DIST_SANDBOX"

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
  ls -l "$DISTNAME.tar.gz" "$DISTNAME.tar.bz2"
  sign_file $DISTNAME.tar.gz $DISTNAME.tar.bz2
  echo ""
  echo "md5sums:"
  md5sum "$DISTNAME.tar.gz" "$DISTNAME.tar.bz2"
  type sha1sum > /dev/null 2>&1
  if [ $? -eq 0 ]; then
    echo ""
    echo "sha1sums:"
    sha1sum "$DISTNAME.tar.gz" "$DISTNAME.tar.bz2"
  fi
else
  ls -l "$DISTNAME.zip"
  sign_file $DISTNAME.zip
  echo ""
  echo "md5sum:"
  md5sum "$DISTNAME.zip"
  type sha1sum > /dev/null 2>&1
  if [ $? -eq 0 ]; then
    echo ""
    echo "sha1sum:"
    sha1sum "$DISTNAME.zip"
  fi
fi
