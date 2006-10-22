#!/usr/bin/env python

#
# header_wrappers.py: Generates SWIG proxy wrappers around Subversion
#                     header files
#

import os, re, string, sys, glob, shutil
if __name__ == "__main__":
  parent_dir = os.path.dirname(os.path.abspath(os.path.dirname(sys.argv[0])))
  sys.path[0:0] = [ parent_dir, os.path.dirname(parent_dir) ]
from gen_base import unique, native_path, build_path_basename, build_path_join
import generator.swig

class Generator(generator.swig.Generator):
  """Generate SWIG proxy wrappers around Subversion header files"""

  def __init__(self, conf, swig_path):
    """Initialize Generator object"""
    generator.swig.Generator.__init__(self, conf, swig_path)

    # Build list of header files
    self.header_files = map(native_path, self.includes)
    self.header_basenames = map(os.path.basename, self.header_files)

  # Ignore svn_repos_parse_fns_t because SWIG can't parse it
  _ignores = ["svn_repos_parse_fns_t"]

  def write_makefile_rules(self, makefile):
    """Write makefile rules for generating SWIG wrappers for Subversion
    header files."""
    wrapper_fnames = []
    python_script = '$(abs_srcdir)/build/generator/swig/header_wrappers.py'
    makefile.write('GEN_SWIG_WRAPPER = cd $(top_srcdir) && $(PYTHON)' +
                   ' %s build.conf $(SWIG)\n\n'  % python_script)
    for fname in self.includes:
      wrapper_fname = build_path_join(self.proxy_dir,
        self.proxy_filename(build_path_basename(fname)))
      wrapper_fnames.append(wrapper_fname)
      makefile.write(
        '%s: %s %s\n' % (wrapper_fname, fname, python_script) +
        '\t$(GEN_SWIG_WRAPPER) %s\n\n' % fname
      )
    makefile.write('SWIG_WRAPPERS = %s\n\n' % string.join(wrapper_fnames))
    for short_name in self.short.values():
      makefile.write('autogen-swig-%s: $(SWIG_WRAPPERS)\n' % short_name)
    makefile.write('\n\n')

  def proxy_filename(self, include_filename):
    """Convert a .h filename into a _h.swg filename"""
    return string.replace(include_filename,".h","_h.swg")

  def _write_nodefault_calls(self, structs):
    """Write proxy definitions to a SWIG interface file"""
    self.ofile.write("\n/* No default constructors for opaque structs */\n")
    self.ofile.write('#ifdef SWIGPYTHON\n');
    for structName, structDefinition in structs:
      if not structDefinition:
        self.ofile.write('%%nodefault %s;\n' % structName)
    self.ofile.write('#endif\n');

  def _write_includes(self, includes, base_fname):
    """Write includes to a SWIG interface file"""

    self.ofile.write('\n/* Includes */\n')

    # Include dependencies
    self.ofile.write('#ifdef SWIGPYTHON\n');
    apr_included = None
    self.ofile.write('%import proxy.swg\n')
    for include in includes:
      if include in self.header_basenames:
        self.ofile.write('%%include %s\n' % self.proxy_filename(include))
      elif include[:3] == "apr" and not apr_included:
        apr_included = 1
        self.ofile.write('%import apr.swg\n')
    self.ofile.write('#endif\n');

    # Include the headerfile itself
    self.ofile.write('%%{\n#include "%s"\n%%}\n' % base_fname)
    if base_fname not in self._ignores:
      self.ofile.write('%%include %s\n' % base_fname)

  def _write_callbacks(self, callbacks):
    """Write invoker functions for callbacks"""
    self.ofile.write('\n/* Callbacks */\n')
    self.ofile.write("\n%inline %{\n")

    struct = None
    for match in callbacks:

      if match[0]:
        struct = match[0]
      elif struct not in self._ignores:
        name, params = match[1:]

        if params == "void":
          param_names = ""
        else:
          param_names = string.join(self._re_param_names.findall(params), ", ")

        params = string.join(string.split(params))
        self.ofile.write(
          "static svn_error_t *%s_invoke_%s(\n" % (struct[:-2], name) +
          "  %s *_obj, %s) {\n" % (struct, params) +
          "  return _obj->%s(%s);\n" % (name, param_names) +
          "}\n\n")

    self.ofile.write("%}\n")

  def _write_proxy_definitions(self, structs):
    """Write proxy definitions to a SWIG interface file"""
    self.ofile.write('\n/* Structure definitions */\n')
    self.ofile.write('#ifdef SWIGPYTHON\n');
    for structName, structDefinition in structs:
      if structDefinition:
        self.ofile.write('%%proxy(%s);\n' % structName)
      else:
        self.ofile.write('%%opaque_proxy(%s);\n' % structName)
    self.ofile.write('#endif\n');

  """Regular expression for parsing includes from a C header file"""
  _re_includes = re.compile(r'#\s*include\s*[<"]([^<">;\s]+)')

  """Regular expression for parsing structs from a C header file"""
  _re_structs = re.compile(r'\btypedef\s+(?:struct|union)\s+'
                           r'(svn_[a-z_0-9]+)\b\s*(\{?)')

  """Regular expression for parsing callbacks from a C header file"""
  _re_callbacks = re.compile(r'\btypedef\s+(?:struct|union)\s+'
                             r'(svn_[a-z_0-9]+)\b|'
                             r'\n\s*svn_error_t\s*\*\(\*(\w+)\)\s*\(([^)]+)\);')

  """Regular expression for parsing parameter names from a parameter list"""
  _re_param_names = re.compile(r'\b(\w+)\s*\)*\s*(?:,|$)')

  """Regular expression for parsing comments"""
  _re_comments = re.compile(r'/\*.*?\*/')

  def _write_swig_interface_file(self, base_fname, includes, structs,
      callbacks):
    """Convert a header file into a SWIG header file"""

    # Calculate output filename from base filename
    output_fname = os.path.join(self.proxy_dir,
      self.proxy_filename(base_fname))

    # Open the output file
    self.ofile = open(output_fname, 'w')
    self.ofile.write('/* Proxy classes for %s\n' % base_fname)
    self.ofile.write(' * DO NOT EDIT -- AUTOMATICALLY GENERATED */\n')

    # Write list of structs for which we shouldn't define constructors
    # by default
    self._write_nodefault_calls(structs)

    # Write includes into the SWIG interface file
    self._write_includes(includes, base_fname)

    # Write proxy definitions into the SWIG interface file
    self._write_proxy_definitions(structs)

    # Write callback definitions into the SWIG interface file
    self._write_callbacks(callbacks)

    # Close our output file
    self.ofile.close()

  def process_header_file(self, fname):
    """Generate a wrapper around a header file"""

    # Read the contents of the header file
    contents = open(fname).read()

    # Remove comments
    contents = self._re_comments.sub("", contents)

    # Get list of includes
    includes = unique(self._re_includes.findall(contents))

    # Get list of structs
    structs = unique(self._re_structs.findall(contents))

    # Get list of callbacks
    callbacks = self._re_callbacks.findall(contents)

    # Get the location of the output file
    base_fname = os.path.basename(fname)

    # Write the SWIG interface file
    self._write_swig_interface_file(base_fname, includes, structs, callbacks)

  def write(self):
    """Generate wrappers for all header files"""

    for fname in self.header_files:
      self.process_header_file(fname)

if __name__ == "__main__":
  if len(sys.argv) < 3:
    print """Usage: %s build.conf swig [ subversion/include/header_file.h ]
Generates SWIG proxy wrappers around Subversion header files. If no header
files are specified, generate wrappers for subversion/include/*.h. """
  else:
    gen = Generator(sys.argv[1], sys.argv[2])
    if len(sys.argv) > 3:
      for fname in sys.argv[3:]:
        gen.process_header_file(fname)
    else:
      gen.write()
