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

# Produce all the `Makefile.in's, verbosely, and create neat missing things
# like `libtool', `install-sh', etc.
automake --add-missing --verbose

# Produce aclocal.m4, so autoconf gets the automake macros it needs
echo "Creating aclocal.m4..."
aclocal -I ac-helpers

# Produce ./configure
echo "Creating configure..."
autoconf

# Produce config.h.in
echo "Creating config.h.in..."
autoheader

# Meta-configure apr/ subdir
if [ -d apr ]; then
  echo "Creating config files for APR..."
  (cd apr; ./buildconf)  # this is apr's equivalent of autogen.sh
else
  echo ""
  echo "You don't have an apr/ subdirectory here.  Please get one:"
  echo ""
  echo "   cvs -d :pserver:anoncvs@www.apache.org:/home/cvspublic login"
  echo "      (password 'anoncvs')"
  echo ""
  echo "   cvs -d :pserver:anoncvs@www.apache.org:/home/cvspublic \\"
  echo "          checkout -d apr apache-2.0/src/lib/apr"
  echo ""
  echo "Run that right here in the top-level of the Subversion tree."
  echo ""
  exit 1
fi

# Handle the neon/ subdir
if [ ! -d neon ]; then
  echo ""
  echo "You don't have a neon/ subdirectory here.  Please get the latest"
  echo "Neon distribution from:"
  echo "    http://www.webdav.org/neon/"
  echo ""
  echo "Unpack the archive using tar/gunzip and rename the resulting"
  echo "directory from ./neon-X.Y.Z/ to ./neon/"
  echo ""
  exit 1
fi

# toss our old config.cache. it may be invalid now.
rm -f config.cache

echo ""
echo "You can run ./configure now."
echo ""
echo "Running autogen.sh implies you are a maintiner. You may be interested"
echo "in using the --with-maintainer-mode option:"
echo ""
echo "   ./configure --with-maintainer-mode"
echo ""
echo "Or if you're using GDB, you might prefer static linking:"
echo ""
echo "   ./configure --disable-shared"
echo ""
echo "Enjoy."
