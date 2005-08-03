#
# gen_swig.py -- generate SWIG interface files
#

import gen_base
import gen_make
import os, re, string
from gen_base import unique, native_path

class Generator(gen_make.Generator):
  """Generate SWIG interface files"""
 
  # Ignore svn_md5.h because SWIG can't parse it
  _ignores = ["svn_md5.h"];

  def __init__(self, fname, verfname, options=None):
    gen_base.GeneratorBase.__init__(self, fname, verfname, options)

  def _proxy_filename(self, include_filename):
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
      if include in self._include_basenames:
        self.ofile.write('%%include %s\n' % self._proxy_filename(include))
      elif include[:3] == "apr" and not apr_included:
        apr_included = 1
        self.ofile.write('%import apr.swg\n')
    self.ofile.write('#endif\n');

    # Include the headerfile itself
    self.ofile.write('%%{\n#include "%s"\n%%}\n' % base_fname)
    if base_fname not in self._ignores:
      self.ofile.write('%%include %s\n' % base_fname)

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
  _re_structs = re.compile(r'\btypedef\s+struct\s+(svn_[a-z_0-9]+_t)\b\s*(\{?)')

  def _write_swig_interface_file(self, base_fname, includes, structs):
    """Convert a header file into a SWIG header file"""

    # Calculate output filename from base filename
    output_fname = os.path.join(self.swig_proxy_dir,
      self._proxy_filename(base_fname))

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

    # Close our output file
    self.ofile.close()

  def _process_header_file(self, fname):

    # Read the contents of the header file
    contents = open(fname).read()

    # Write includes into the SWIG interface file
    includes = unique(self._re_includes.findall(contents))

    # Get list of structs
    structs = unique(self._re_structs.findall(contents))

    # Get the location of the output file
    base_fname = os.path.basename(fname)

    self._write_swig_interface_file(base_fname, includes, structs)

  def write(self):
    """Generate SWIG interface files"""
    header_files = map(native_path, self.includes)
    self._include_basenames = map(os.path.basename, header_files)
    for fname in header_files:
      if fname[-2:] == ".h":
        self._process_header_file(fname)
