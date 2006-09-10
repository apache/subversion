#!/bin/sh

### Purify a system, to simulate building Subversion on a "clean" box.
###
### You'll probably need to run this as `root', and may need to change
### some paths for your system.

# Clean out old apr, apr-util config scripts.
rm /usr/local/bin/apr-config
rm /usr/local/bin/apu-config

# Clean out libs.
rm -f /usr/local/lib/APRVARS
rm -f /usr/local/lib/libapr*
rm -f /usr/local/lib/libexpat*
rm -f /usr/local/lib/libneon*
rm -f /usr/local/lib/libsvn*

# Clean out headers.
rm -f /usr/local/include/apr*
rm -f /usr/local/include/svn*
rm -f /usr/local/include/neon/*

### Not sure this would be useful:
# rm -f /usr/local/apache2/lib/*
