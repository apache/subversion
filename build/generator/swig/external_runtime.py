#!/usr/bin/env python

#
# external_runtime.py: Generate external runtime files for SWIG
#

import sys, os
if __name__ == "__main__":
  parent_dir = os.path.dirname(os.path.abspath(os.path.dirname(sys.argv[0])))
  sys.path[0:0] = [ parent_dir, os.path.dirname(parent_dir) ]
import generator.swig
import generator.util.executable
_exec = generator.util.executable

class Generator(generator.swig.Generator):
  """Generate external runtime files for SWIG"""

  def write(self):
    """Generate external runtime and long long fix"""
    self.write_external_runtime()
    self.write_long_long_fix()

  def write_external_runtime(self):
    """Generate external runtime header files for each SWIG language"""

    # Gather necessary header files
    self.checkout(".","swigrun.swg")
    if self.version() == 103024:
      self.checkout(".","common.swg")
    self.checkout(".","runtime.swg")
    self.checkout("python","pyrun.swg")
    self.checkout("perl5","perlrun.swg")
    self.checkout("ruby","rubydef.swg")
    self.checkout("ruby", "rubyhead.swg")

    # Runtime library names
    runtime_library = {
      "python": "pyrun.swg", "perl":"perlrun.swg", "ruby":"rubydef.swg"
    }

    # Build runtime files
    for lang in self.langs:
      out = "%s/swig_%s_external_runtime.swg" % (self.proxy_dir, lang)
      if self.version() == 103024:
        out_file = open(out, "w")
        out_file.write(open("%s/swigrun.swg" % self.proxy_dir).read())
        out_file.write(open("%s/common.swg" % self.proxy_dir).read())
        out_file.write(
          open("%s/%s" % (self.proxy_dir, runtime_library[lang])).read())
        if lang != "ruby":
          out_file.write(open("%s/runtime.swg" % self.proxy_dir).read())
        out_file.close()
      else:
        _exec.run("%s -%s -external-runtime %s" % (self.swig_path, lang, out))

  def write_long_long_fix(self):
    """Hide the SWIG implementation of 'long long' converters so that
       Visual C++ won't get confused by it."""

    self.checkout("python","python.swg")

    python_swg_filename = "%s/python.swg" % self.proxy_dir
    python_swg = open(python_swg_filename).read()
    file = open(python_swg_filename,"w")
    file.write("""
    %fragment("SWIG_AsVal_" {long long},"header") {
    }
    %fragment("SWIG_Check_" {long long},"header") {
    }
    %fragment("SWIG_From_" {long long},"header") {
    }\n""")
    file.write(python_swg)
    file.close()

  def write(self):
    self.write_external_runtime()
    self.write_long_long_fix()

if __name__ == "__main__":
  if len(sys.argv) < 3:
    print "Usage: %s build.conf swig"
    print "Generates external runtime files for SWIG"
  else:
    gen = Generator(sys.argv[1], sys.argv[2])
    gen.write()
