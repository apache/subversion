#!/usr/bin/env python

#
# This script copies SWIG runtime code from the SWIG library directory into
# C source files that be compiled into DLLs. It's based on SWIG's 
# Runtime/Makefile.in file.
#

import os, sys, string

class Literal:
  def __init__(self, value):
    self.value = value

  def write(self, out):
    out.write(self.value);

class File:
  def __init__(self, *path_parts):
    self.path = os.path.join(*path_parts)

  def write(self, out):
    fp = open(os.path.join(SWIG_LIB, self.path), 'rt')
    try:
      while 1:
        chunk = fp.read(4096)
        if not chunk: break
        out.write(chunk)
    finally:
      fp.close()

languages = {

  'tcl':      (File('common.swg'),
               File('tcl', 'swigtcl8.swg')),

  'python':   (Literal('#include "Python.h"\n'),
               File('common.swg'),
               File('python', 'pyrun.swg')),

  'perl':     (File('common.swg'),
               File('perl5', 'perlrun.swg')),

  'ruby':     (File('common.swg'),
               File('ruby', 'rubyhead.swg'),
               File('ruby', 'rubydef.swg')),

  'guile':    (File('guile', 'guiledec.swg'),
               File('guile', 'guile.swg')),

  'mzscheme': (File('mzscheme', 'mzschemedec.swg'),
               File('mzscheme', 'mzscheme.swg')),

  'php':      (File('common.swg'),
               File('php4', 'php4run.swg')),

  'ocaml':    (File('ocaml', 'libswigocaml.swg')),

  'pike':     (File('common.swg'),
               File('pike', 'pikerun.swg')),

  'chicken':  (Literal('#include "chicken.h"\n'),
               File('common.swg'),
               File('chicken', 'chickenrun.swg')),

}

if __name__ == "__main__":
  if len(sys.argv) != 3:
    print >> sys.stderr, 'Usage: %s language output.c' % sys.argv[0]
    sys.exit(1)

  language = sys.argv[1]
  output = sys.argv[2]

  fp = os.popen('swig -swiglib', 'r')
  try:
    SWIG_LIB = string.rstrip(fp.readline())
  finally:
    fp.close()

  if not SWIG_LIB:
    print >> sys.stderr, "Error: `swig -swiglib` returned nothing, unable to"
    print >> sys.stderr, "detect swig library directory. Make sure swig is on"
    print >> sys.stderr, "the standard path."
    sys.exit(1)

  contents = languages[language]
  fp = open(output, 'wt')
  try:
    for c in contents:
      c.write(fp)
  finally:
    fp.close()
