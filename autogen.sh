### Run this to produce everything needed for configuration. ###

# Produce all the `Makefile.in's
automake --add-missing

# Produce aclocal.m4, so autoconf gets the automake macros it needs
aclocal

# Produce ./configure
autoconf

# Produce config.h.in
autoheader

echo ""
echo "Note: *Don't* run configure yet!  (Once autoconfiscation is complete, "
echo "this message will go away.)"
echo ""