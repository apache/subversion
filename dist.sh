#!/bin/sh

##########################################################################
# How to build a Subversion distribution tarball:
#
# Run this script in the top-level of a configured working copy that
# has apr/ and neon/ subdirs, and you'll end up with
# subversion-X.Y.tar.gz in that top-level directory.
#
# The tarball will be based on the HEAD of the repository, not on
# whatever set of revisions are in your working copy.  However, since
# the documentation will be produced by running "make doc" on your
# working copy's revisions of the doc master files, it's probably
# simplest if you just make sure your working copy is at HEAD as well.
# Then you won't get any unexpected results.
#
# Note: currently, you'll also need a line in your ~/.cvspass about
# ":pserver:guest@cvs.tigris.org:/cvs", for the export.  If you've
# been following the Subversion project, chances are you already have
# such a line.
##########################################################################

### Rolling block.
DIST_SANDBOX=.dist_sandbox

### The "X.Y" part of ${DISTNAME}-X.Y.tar.gz
VERSION=`grep SVN_VERSION configure.in | cut -f 2 -d ' ' | sed -e 's/"//g' | sed -e 's/,//g'`

### The tarball's basename, also the name of the subdirectory into which
### it should unpack.
DISTNAME=subversion-${VERSION}

### Clean up the old docs so we're guaranteed the latest ones.
# This is necessary only because "make clean" doesn't appear
# to clean up docs at the moment.
rm -f doc/programmer/design/svn-design.info
rm -f doc/programmer/design/svn-design.info-*
rm -f doc/programmer/design/svn-design.html
rm -f doc/programmer/design/svn-design.txt
rm -f doc/user/manual/svn-manual.info
rm -f doc/user/manual/svn-manual.html
rm -f doc/user/manual/svn-manual.txt

### Build new docs.
make doc

### Prepare an area for constructing the dist tree.
rm -rf ${DIST_SANDBOX}
mkdir ${DIST_SANDBOX}

### Export the dist tree, clean it up.
(cd ${DIST_SANDBOX}; cvs -d :pserver:guest@cvs.tigris.org:/cvs \
                       export -D tomorrow -d ${DISTNAME} subversion)
rm -rf `find ${DIST_SANDBOX}/${DISTNAME} -name .cvsignore -print`

### Ship with (relatively) clean APR and neon working copies
### inside the tarball, just to make people's lives easier.
cp -r apr ${DIST_SANDBOX}/${DISTNAME}
(cd ${DIST_SANDBOX}/${DISTNAME}/apr; make distclean)
# Defang the APR working copy.
rm -rf `find ${DIST_SANDBOX}/${DISTNAME}/apr -name CVS -type d -print`
rm -rf `find ${DIST_SANDBOX}/${DISTNAME}/apr -name .cvsignore -print`
# Clean most of neon.
cp -r neon ${DIST_SANDBOX}/${DISTNAME}
(cd ${DIST_SANDBOX}/${DISTNAME}/neon; make distclean)
# Then do some extra cleaning in neon, since its `make distclean'
# rule still leaves some .o files lying around.  Better to
# patch Neon, of course; but the fix wasn't obvious to me --
# something to do with @NEONOBJS@ in neon/src/Makefile.in?
rm -f ${DIST_SANDBOX}/${DISTNAME}/neon/src/*.o

### Run autogen.sh in the dist, so we ship with a configure script.
# First make sure autogen.sh is executable, because, as Mike Pilato
# points out, until we get permission versioning working, it won't be
# executable on export from svn.
chmod a+x ${DIST_SANDBOX}/${DISTNAME}/autogen.sh
(cd ${DIST_SANDBOX}/${DISTNAME}; ./autogen.sh)

### Copy all the pre-built docs, so we ship with ready documentation.
for name in doc/programmer/design/svn-design.info   \
            doc/programmer/design/svn-design.info-* \
            doc/programmer/design/svn-design.html   \
            doc/programmer/design/svn-design.txt    \
            doc/user/manual/svn-manual.info         \
            doc/user/manual/svn-manual.html         \
            doc/user/manual/svn-manual.txt
do
   cp ${name} ${DIST_SANDBOX}/${DISTNAME}/${name}
done

### Tell people where to find old information.
cat > ${DIST_SANDBOX}/${DISTNAME}/ChangeLog.CVS <<EOF
The old CVS ChangeLog is kept at 

     http://subversion.tigris.org

If you want to see changes since Subversion went self-hosting,
you probably want to use the "svn log" command -- and if it 
does not do what you need, please send in a patch!
EOF

### Make the tarball.
echo "Rolling ${DISTNAME}.tar.gz ..."
(cd ${DIST_SANDBOX}; tar zcvpf ${DISTNAME}.tar.gz ${DISTNAME})

### Copy it upstairs and clean up.
cp ${DIST_SANDBOX}/${DISTNAME}.tar.gz .
rm -rf ${DIST_SANDBOX}

echo ""
echo "Done:"
ls -l ${DISTNAME}.tar.gz
echo ""
