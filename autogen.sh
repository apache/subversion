### Run this to produce everything needed for configuration. ###

# Produce all the `Makefile.in's
automake --add-missing

# Produce aclocal.m4, so autoconf gets the automake macros it needs
aclocal

# Produce ./configure
autoconf

# Produce config.h.in
autoheader

# Meta-configure apr/ subdir
# Meta-configure apr/ subdir
if [ -d apr ]; then
  (cd apr; ./buildconf)
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
echo "(Note that autoconfiscation is still in progress, so not everything"
echo "will be built.)"
echo ""
