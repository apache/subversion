#!/bin/sh

#
# USAGE: ./dist.sh [VERSION [NAME [REPOS-PATH]]]
#
#   Create a distribution tarball, labelling it with the given version. If
#   the version is not supplied, the HEAD version will be used.
#
#   If NAME is supplied, it will be used in the version string. From CVS
#   trees, this is 'dev build'. By default, this will be 'r<VERSION>'.
#   Note that you can use '' or 'HEAD' for the VERSION to be able to
#   supply a name in the second argument.
#
#   If REPOS-PATH is supplied, the tarball will be constructed from the
#   root located at that path (e.g. /branches/foo). If REPOS-PATH is not
#   supplied, then /trunk will be used.
#
#   Note: the leading slash on REPOS-PATH will be inserted if not present.
#

##########################################################################
# How to build a Subversion distribution tarball:
#
# Run this script in the top-level of a configured working copy that
# has apr, apr-util, and neon subdirs, and you'll end up with
# subversion-rXXX.tar.gz in that top-level directory.
#
# Unless specified otherwise (with a single REVISION argument to this
# script), the tarball will be based on the HEAD of the repository.
#
# It will *not* be based on whatever set of revisions are in your
# working copy.  However, since 
#
#   - the documentation will be produced by running "make doc" 
#     on your working copy's revisions of the doc master files, and
#
#   - since the APR and APRUTIL trees are basically copied from your working 
#     copy, 
#
# it's probably simplest if you just make sure your working copy is at
# the same revision as that of the distribution you are trying to
# create.  Then you won't get any unexpected results.
#
##########################################################################

### Rolling block.
DIST_SANDBOX=.dist_sandbox

### Estimated current version of your working copy
WC_VERSION=`svn st -vN doc/README | awk '{print $1}'`

### The "REV" part of ${DISTNAME}-rREV.tar.gz
if test -z "$1" || test "$1" = "HEAD"; then
  VERSION="`svn st -vu README | tail -1 | awk '{print $3}'`"
else
  VERSION="$1"
fi

RELEASE_NAME="$2"
if test -z "$RELEASE_NAME"; then
  RELEASE_NAME="r$VERSION"
fi

REPOS_PATH="$3"
if test -z "$REPOS_PATH"; then
  REPOS_PATH="trunk"
else
  # remove any leading slashes
  REPOS_PATH="`echo $REPOS_PATH | sed 's/^\/*//'`"
fi

### The tarball's basename, also the name of the subdirectory into which
### it should unpack.
DISTNAME=subversion-${RELEASE_NAME}
echo "Distribution will be named: ${DISTNAME}"
echo "     constructed from path: /${REPOS_PATH}"

### Warn the user if their working copy looks to be out of sync with
### their requested (or default) revision
if test ${WC_VERSION} != ${VERSION}; then
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

### Clean up the old docs so we're guaranteed the latest ones.
# This is necessary only because "make clean" doesn't appear
# to clean up docs at the moment.
echo "Cleaning old docs in docs/ ..."
rm -f doc/programmer/design/svn-design.info
rm -f doc/programmer/design/svn-design.info-*
rm -f doc/programmer/design/svn-design.html
rm -f doc/programmer/design/svn-design.txt
rm -f doc/handbook/svn-handbook.info
rm -f doc/handbook/svn-handbook.info-*
rm -f doc/handbook/svn-handbook.html
rm -f doc/handbook/svn-handbook.txt
rm -f doc/handbook/translations/french/svn-handbook-french.info
rm -f doc/handbook/translations/french/svn-handbook-french.info-*
rm -f doc/handbook/translations/french/svn-handbook-french.html
rm -f doc/handbook/translations/french/svn-handbook-french.txt

### Build new docs.
echo "Building new docs in docs/ ..."
make doc

### Prepare an area for constructing the dist tree.
rm -rf ${DIST_SANDBOX}
mkdir ${DIST_SANDBOX}
echo "Removed and recreated ${DIST_SANDBOX}"

### Export the dist tree, clean it up.
echo "Exporting revision ${VERSION} of Subversion into sandbox..."
(cd ${DIST_SANDBOX} && \
 svn export -q -r ${VERSION} http://svn.collab.net/repos/svn/$REPOS_PATH \
        ${DISTNAME} --username none --password none)

### Ship with (relatively) clean APR, APRUTIL, and neon working copies
### inside the tarball, just to make people's lives easier.
echo "Copying apr into sandbox, making clean..."
cp -r apr ${DIST_SANDBOX}/${DISTNAME}
(cd ${DIST_SANDBOX}/${DISTNAME}/apr && make extraclean)
# Defang the APR working copy.
echo "Removing all CVS/ and .cvsignore files from apr..."
rm -rf `find ${DIST_SANDBOX}/${DISTNAME}/apr -name CVS -type d -print`
rm -rf `find ${DIST_SANDBOX}/${DISTNAME}/apr -name .cvsignore -print`

echo "Copying apr-util into sandbox, making clean..."
cp -r apr-util ${DIST_SANDBOX}/${DISTNAME}
(cd ${DIST_SANDBOX}/${DISTNAME}/apr-util && make extraclean)
# Defang the APRUTIL working copy.
echo "Removing all CVS/ and .cvsignore files from apr-util..."
rm -rf `find ${DIST_SANDBOX}/${DISTNAME}/apr-util -name CVS -type d -print`
rm -rf `find ${DIST_SANDBOX}/${DISTNAME}/apr-util -name .cvsignore -print`

# Clean most of neon.
echo "Coping neon into sandbox, making clean..."
cp -r neon ${DIST_SANDBOX}/${DISTNAME}
(cd ${DIST_SANDBOX}/${DISTNAME}/neon && make distclean)
# Then do some extra cleaning in neon, since its `make distclean'
# rule still leaves some .o files lying around.  Better to
# patch Neon, of course; but the fix wasn't obvious to me --
# something to do with @NEONOBJS@ in neon/src/Makefile.in?
echo "Cleaning *.o in neon..."
rm -f ${DIST_SANDBOX}/${DISTNAME}/neon/src/*.o

# Remove any config.nice files that may have been left behind. They aren't
# cleaned by anything.
files="`find ${DIST_SANDBOX}/${DISTNAME} -name config.nice -print`"
if test -n "$files"; then
  echo "Removing: $files"
  rm -rf $files
fi

### Run autogen.sh in the dist, so we ship with a configure script.
# First make sure autogen.sh is executable, because, as Mike Pilato
# points out, until we get permission versioning working, it won't be
# executable on export from svn.
echo "Running ./autogen.sh in sandbox, to create ./configure ..."
chmod a+x ${DIST_SANDBOX}/${DISTNAME}/autogen.sh
(cd ${DIST_SANDBOX}/${DISTNAME} && ./autogen.sh)

### Copy all the pre-built docs, so we ship with ready documentation.
echo "Copying new docs into sandbox..."
for name in doc/programmer/design/svn-design.info   \
            doc/programmer/design/svn-design.info-* \
            doc/programmer/design/svn-design.html   \
            doc/programmer/design/svn-design.txt    \
            doc/handbook/svn-handbook.info          \
            doc/handbook/svn-handbook.info-*        \
            doc/handbook/svn-handbook.html          \
            doc/handbook/svn-handbook.txt	    \
            doc/handbook/translations/french/svn-handbook-french.info         \
            doc/handbook/translations/french/svn-handbook-french.info-*       \
            doc/handbook/translations/french/svn-handbook-french.html         \
            doc/handbook/translations/french/svn-handbook-french.txt
do
   cp ${name} ${DIST_SANDBOX}/${DISTNAME}/${name}
done

### Tell people where to find old information.
cat > ${DIST_SANDBOX}/${DISTNAME}/ChangeLog.CVS <<EOF
The old CVS ChangeLog is kept at 

     http://subversion.tigris.org/

If you want to see changes since Subversion went self-hosting,
you probably want to use the "svn log" command -- and if it 
does not do what you need, please send in a patch!
EOF

### Give this release a unique name, to help us interpret bug reports
vsn_file="${DIST_SANDBOX}/${DISTNAME}/subversion/include/svn_version.h"
sed -e \
 "/#define *SVN_VER_TAG/s/dev build/${RELEASE_NAME}/" \
  < "$vsn_file" > "${vsn_file}.tmp"

mv "${vsn_file}.tmp" "$vsn_file"


### Make the tarball.
echo "Rolling ${DISTNAME}.tar.gz ..."
(cd ${DIST_SANDBOX} && tar zcpf ${DISTNAME}.tar.gz ${DISTNAME})

### Copy it upstairs and clean up.
echo "Copying tarball out, removing sandbox..."
cp ${DIST_SANDBOX}/${DISTNAME}.tar.gz .
rm -rf ${DIST_SANDBOX}

echo ""
echo "Done:"
ls -l ${DISTNAME}.tar.gz
echo ""
