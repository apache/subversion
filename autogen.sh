#!/bin/sh

### Run this to produce everything needed for configuration. ###

# Ensure some permissions for executables used by this script
for execfile in gen-make.py \
                dist.sh \
                build/buildcheck.sh \
                build/getversion.py \
                build/PrintPath \
                ac-helpers/get-neon-ver.sh \
                ac-helpers/install-sh; do
  chmod +x $execfile                
done


# Run tests to ensure that our build requirements are met
NEON_CHECK_CONTROL=""
if test "$1" = "--disable-neon-version-check"; then
    NEON_CHECK_CONTROL="$1"
    shift
fi
./build/buildcheck.sh $NEON_CHECK_CONTROL || exit 1

### temporary cleanup during transition to libtool 1.4
(cd ac-helpers ; rm -f ltconfig ltmain.sh libtool.m4)

#
# Handle some libtool helper files
#
# ### eventually, we can/should toss this in favor of simply using
# ### APR's libtool. deferring to a second round of change...
#

libtoolize="`./build/PrintPath glibtoolize libtoolize`"

if [ "x$libtoolize" = "x" ]; then
    echo "libtoolize not found in path"
    exit 1
fi

$libtoolize --copy --automake

ltpath="`dirname $libtoolize`"
ltfile="`cd $ltpath/../share/aclocal ; pwd`"/libtool.m4

if [ ! -f $ltfile ]; then
    echo "$ltfile not found"
    exit 1
fi

echo "Copying libtool helper: $ltfile"
cp $ltfile ac-helpers/libtool.m4

# This is just temporary until people's workspaces are cleared -- remove
# any old aclocal.m4 left over from prior build so it doesn't cause errors.
rm -f aclocal.m4

# Produce getdate.c from getdate.y.
# Again, this means that "developers" who run autogen.sh need either
# yacc or bison -- but not people who compile sourceballs, since `make
# dist` will include getdate.c.
echo "Creating getdate.c..."
bison -o subversion/libsvn_subr/getdate.c subversion/libsvn_subr/getdate.y
if [ $? -ne 0 ]; then
    yacc -o subversion/libsvn_subr/getdate.c subversion/libsvn_subr/getdate.y
    if [ $? -ne 0 ]; then
        echo
        echo "   Error:  can't find either bison or yacc."
        echo "   One of these is needed to generate the date parser."
        echo
        exit 1
    fi
fi

# Create the file detailing all of the build outputs for SVN.
#
# Note: this dependency on Python is fine: only SVN developers use autogen.sh
#       and we can state that dev people need Python on their machine. Note
#       that running gen-make.py requires Python 1.X or newer.

OK=`python -c 'print "OK"'`
if test "${OK}" != "OK" ; then
  echo "Python check failed, make sure python is installed and on the PATH"
  exit 1
fi

if test "$1" = "-s"; then
  echo "Creating build-outputs.mk (no dependencies)..."
  ./gen-make.py -s build.conf ;
else
  echo "Creating build-outputs.mk..."
  ./gen-make.py build.conf ;
fi

if test "$?" != "0"; then
  echo "gen-make.py failed"
  exit 1
fi

# Produce config.h.in
# Do this before the automake (automake barfs if the header isn't available).
# Do it after the aclocal command -- automake sets up the header to depend
# on aclocal.m4
echo "Creating svn_private_config.h.in..."
${AUTOHEADER:-autoheader}

# If there's a config.cache file, we may need to delete it.  
# If we have an existing configure script, save a copy for comparison.
if [ -f config.cache ] && [ -f configure ]; then
  cp configure configure.$$.tmp
fi

# Produce ./configure
echo "Creating configure..."
${AUTOCONF:-autoconf}

# If we have a config.cache file, toss it if the configure script has
# changed, or if we just built it for the first time.
if [ -f config.cache ]; then
  (
    [ -f configure.$$.tmp ] && cmp configure configure.$$.tmp > /dev/null 2>&1
  ) || (
    echo "Tossing config.cache, since configure has changed."
    rm config.cache
  )
  rm -f configure.$$.tmp
fi

# Remove autoconf 2.5x's cache directory
rm -rf autom4te*.cache

# Run apr/buildconf if it exists.
if test -x "apr/buildconf" ; then
  echo "Creating configuration files for apr." # apr's equivalent of autogen.sh
  (cd apr && ./buildconf)
fi

# Run apr-util/buildconf if it exists.
if test -x "apr-util/buildconf" ; then
  echo "Creating configuration files for apr-util."
  (cd apr-util && ./buildconf)
fi

echo ""
echo "You can run ./configure now."
echo ""
echo "Running autogen.sh implies you are a maintainer.  You may prefer"
echo "to run configure in one of the following ways:"
echo ""
echo "./configure --enable-maintainer-mode"
echo "./configure --disable-shared"
echo "./configure --enable-maintainer-mode --disable-shared"
echo ""
echo "Note:  this build will create the Subversion shared libraries and a"
echo "       command-line client.  If you wish to build a Subversion server,"
echo "       you will need Apache 2.0.  See the INSTALL file for details."
echo ""
