### Run this to produce everything needed for configuration. ###

# First check that we're using at least automake 1.4, lest
#  we get infinite loops from the command `SUBDIRS = . tests'
automake --version | perl -ne 'if (/\(GNU automake\) ([0-9].[0-9])/) {print;  if ($1 < 1.4) {exit 1;}}'

if [ $? -ne 0 ]; then
    echo "Error: you need automake 1.4 or later.  Please upgrade."
    exit 1
fi

# Produce all the `Makefile.in's, verbosely, and create neat missing things
# like `libtool', `install-sh', etc.
automake --add-missing --verbose

# Produce aclocal.m4, so autoconf gets the automake macros it needs
aclocal

# Produce ./configure
autoconf

# Produce config.h.in
autoheader

# Meta-configure apr/ subdir
if [ -d apr ]; then
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

echo ""
echo "You can run ./configure now."
echo ""
echo "(Note that this software is still a work in progress.)"
echo ""
