#!/bin/sh

### Run this to produce everything needed for configuration. ###

# First check that we're using at least automake 1.4, lest
#  we get infinite loops from the command `SUBDIRS = . tests'
#
# Note: this dependency on Perl is fine: only SVN developers use autogen.sh
#       and we can state that dev people need Perl on their machine
#
automake --version | perl -ne 'if (/\(GNU automake\) ([0-9].[0-9])/) {print;  if ($1 < 1.4) {exit 1;}}'

if [ $? -ne 0 ]; then
    echo "Error: you need automake 1.4 or later.  Please upgrade."
    exit 1
fi

# Produce aclocal.m4, so autoconf gets the automake macros it needs
echo "Creating aclocal.m4..."
aclocal -I ac-helpers

# Produce all the `Makefile.in's, verbosely, and create neat missing things
# like `libtool', `install-sh', etc.
automake --add-missing --verbose

# If there's a config.cache file, we may need to delete it.  
# If we have an existing configure script, save a copy for comparison.
if [ -f config.cache ] && [ -f configure ]; then
  cp configure configure.$$.tmp
fi

# Produce ./configure
echo "Creating configure..."
autoconf

# Produce config.h.in
echo "Creating svn_private_config.h.in..."
autoheader

# Meta-configure apr/ subdir
if [ -d apr ]; then
  echo "Creating config files for APR..."
  (cd apr; ./buildconf)  # this is apr's equivalent of autogen.sh
else
  echo ""
  echo "...Uh oh, there is a problem."
  echo "You don't have an apr/ subdirectory here.  Please get one:"
  echo ""
  echo "   cvs -d :pserver:anoncvs@apache.org:/home/cvspublic login"
  echo "      (password 'anoncvs')"
  echo ""
  echo "   cvs -d :pserver:anoncvs@apache.org:/home/cvspublic co apr"
  echo ""
  echo "Run that right here in the top-level of the Subversion tree."
  echo ""
  exit 1
fi

# Handle the neon/ subdir
NEON_VER=0.8.1
if [ ! -d neon ]; then
  echo ""
  echo "...Uh oh, there is a problem."
  echo "You don't have a neon/ subdirectory here."
  echo "Please get neon ${NEON_VER} from:"
  echo "       http://www.webdav.org/neon/neon-${NEON_VER}.tar.gz"
  echo ""
  echo "Unpack the archive using tar/gunzip and rename the resulting"
  echo "directory from ./neon-${NEON_VER}/ to ./neon/"
  echo ""
  exit 1
fi
grep NEON_VERSION=${NEON_VER} neon/configure.in > /dev/null
if [ $? -ne 0 ]; then
  echo ""
  echo "...Uh oh, there is a problem."
  echo "You have a neon/ subdir, but it is not neon ${NEON_VER}."
  echo "Please get neon ${NEON_VER} from:"
  echo "       http://www.webdav.org/neon/neon-${NEON_VER}.tar.gz"
  echo ""
  echo "Unpack the archive using tar/gunzip and rename the resulting"
  echo "directory from ./neon-${NEON_VER}/ to ./neon/"
  echo ""
  exit 1
fi

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

echo ""
echo "You can run ./configure now."
echo ""
echo "Running autogen.sh implies you are a maintainer.  You may prefer"
echo "to run configure in one of the following ways:"
echo ""
echo "./configure --with-maintainer-mode"
echo "./configure --disable-shared"
echo "./configure --with-maintainer-mode --disable-shared"
echo ""
