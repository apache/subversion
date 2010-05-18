#!/usr/bin/env python

import sys
import os
import glob

#######################################################
candidate_xsldirs = (
    # Fedora
    '/usr/share/sgml/docbook/xsl-stylesheets',
    # Cygwin
    '/usr/share/docbook-xsl',
    # Debian
    '/usr/share/xml/docbook/stylesheet/nwalsh',
    # SUSE
    '/usr/share/xml/docbook/stylesheet/nwalsh/current',
    # FreeBSD
    '/usr/local/share/xsl/docbook',
    # Gentoo
    '/usr/share/sgml/docbook/xsl-stylesheets-*',
    # Please add your OS's location here if not listed!
    )
#######################################################

tools_bin_dir = os.path.dirname(sys.argv[0])
xsl_dir = os.path.join(tools_bin_dir, '..', 'xsl')

if os.path.exists(xsl_dir):
  print "XSL directory %s already exists" % (xsl_dir,)
  sys.exit(0)

for i in candidate_xsldirs:
  # Crude method of preferring the highest version, when multiple exist
  globs = sorted(glob.glob(i))
  globs.reverse()
  for j in globs:
    if os.path.exists(os.path.join(j, 'html', 'docbook.xsl')):
      os.symlink(j, xsl_dir)
      print "Found and linked %s" % (j,)
      sys.exit(0)

sys.stderr.write('ERROR: Failed to find a DocBook XSL directory\n')
sys.exit(1)
