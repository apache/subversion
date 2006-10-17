#!/usr/bin/env python

#
# external_runtime.py: Generate external runtime files for SWIG
#

import sys, os, re, fileinput
if __name__ == "__main__":
  parent_dir = os.path.dirname(os.path.abspath(os.path.dirname(sys.argv[0])))
  sys.path[0:0] = [ parent_dir, os.path.dirname(parent_dir) ]
import generator.swig
import generator.util.executable
_exec = generator.util.executable

class Generator(generator.swig.Generator):
  """Generate external runtime files for SWIG"""

  def write(self):
    """Generate external runtimes"""
    for lang in self.langs:
      self.write_external_runtime(lang)

  def write_makefile_rules(self, makefile):
    """Write the makefile rules for generating external runtimes"""
    makefile.write(
      'GEN_SWIG_RUNTIME = cd $(top_srcdir) && $(PYTHON)' +
      ' build/generator/swig/external_runtime.py build.conf $(SWIG)\n\n'
    )
    for lang in self.langs:
      out = self._output_file(lang)
      makefile.write(
        'autogen-swig-%s: %s\n' % (self.short[lang], out) +
        '%s: $(SWIG_CHECKOUT_FILES)\n' % out +
        '\t$(GEN_SWIG_RUNTIME) %s\n\n' % lang
      )
    makefile.write('\n')

  def write_external_runtime(self, lang):
    """Generate external runtime header files for each SWIG language"""

    # Runtime library names
    runtime_library = {
      "python": "pyrun.swg", "perl":"perlrun.swg", "ruby":"rubydef.swg"
    }

    # Build runtime files
    out = self._output_file(lang)
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

    if lang == "ruby" and self.version() >= 103026 and self.version() < 103028:
      # SWIG 1.3.26-27 should include rubytracking.swg in their
      # external runtime, but they don't.
      runtime = open(out).read()
      tracking = open("%s/rubytracking.swg" % self.proxy_dir).read();
      out_file = open(out, "w")
      out_file.write(tracking)
      out_file.write(runtime)
      out_file.close()

    # SWIG 1.3.25 and earlier use the wrong number of arguments in calls to
    # SWIG_GetModule. We fix this below.
    if self.version() <= 103025:
      for line in fileinput.input(out, inplace=1):
        sys.stdout.write(
          re.sub(r"SWIG_GetModule\(\)", "SWIG_GetModule(NULL)", line)
        )
  def _output_file(self, lang):
    """Return the output filename of the runtime for the given language""" 
    return '%s/swig_%s_external_runtime.swg' % (self.proxy_dir, lang)


if __name__ == "__main__":
  if len(sys.argv) != 4:
    print "Usage: %s build.conf swig"
    print "Generates external runtime files for SWIG"
  else:
    gen = Generator(sys.argv[1], sys.argv[2])
    gen.write_external_runtime(sys.argv[3])
