### Run this to produce everything needed for configuration. ###

# Produce all the `Makefile.in's
automake --add-missing

# Produce aclocal.m4, so autoconf gets the automake macros it needs
aclocal

# Produce ./configure
autoconf

# Produce config.h.in
autoheader

echo "Don't run configure yet!  Once we autoconfiscation is working, "
echo "this message will go away."
