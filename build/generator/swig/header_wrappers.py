#!/usr/bin/env python

#
# header_wrappers.py: Generates SWIG proxy wrappers around Subversion
#                     header files
#

import os, re, sys, glob, shutil
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
    self.header_files = list(map(native_path, self.includes))
    self.header_basenames = list(map(os.path.basename, self.header_files))

  # Ignore svn_repos_parse_fns_t because SWIG can't parse it
  _ignores = ["svn_repos_parse_fns_t",
              "svn_auth_gnome_keyring_unlock_prompt_func_t",
              ]

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
    makefile.write('SWIG_WRAPPERS = %s\n\n' % ' '.join(wrapper_fnames))
    for short_name in self.short.values():
      makefile.write('autogen-swig-%s: $(SWIG_WRAPPERS)\n' % short_name)
    makefile.write('\n\n')

  def proxy_filename(self, include_filename):
    """Convert a .h filename into a _h.swg filename"""
    return include_filename.replace(".h","_h.swg")

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
    self.ofile.write('%%{\n#include "%s"\n%%}\n' % base_fname)
    if base_fname not in self._ignores:
      self.ofile.write('%%include %s\n' % base_fname)


  def _write_callback(self, type, return_type, module, function, params,
                      callee):
    """Write out an individual callback"""

    # Get rid of any extra spaces or newlines
    return_type = ' '.join(return_type.split())
    params = ' '.join(params.split())

    # Calculate parameters
    if params == "void":
      param_names = ""
      params = "%s _obj" % type
    else:
      param_names = ", ".join(self._re_param_names.findall(params))
      params = "%s _obj, %s" % (type, params)

    invoke_callback = "%s(%s)" % (callee, param_names)
    if return_type != "void":
      invoke_callback = "return %s" % (invoke_callback)

    # Write out the declaration
    self.ofile.write(
      "static %s %s_invoke_%s(\n" % (return_type, module, function) +
      "  %s) {\n" % params +
      "  %s;\n" % invoke_callback +
      "}\n\n")


  def _write_callback_typemaps(self, callbacks):
    """Apply the CALLABLE_CALLBACK typemap to all callbacks"""

    self.ofile.write('\n/* Callback typemaps */\n')
    types = [];
    for match in callbacks:
      if match[0] and match[1]:
        # Callbacks declared as a typedef
        return_type, module, function, params = match
        type = "%s_%s_t" % (module, function)
        types.append(type)

    if types:
      self.ofile.write(
        "#ifdef SWIGPYTHON\n"
        "%%apply CALLABLE_CALLBACK {\n"
        "  %s\n"
        "};\n"
        "%%apply CALLABLE_CALLBACK * {\n"
        "  %s *\n"
        "};\n"
        "#endif\n" % ( ",\n  ".join(types), " *,\n  ".join(types) )
      );


  def _write_baton_typemaps(self, batons):
    """Apply the PY_AS_VOID typemap to all batons"""

    self.ofile.write('\n/* Baton typemaps */\n')

    if batons:
      self.ofile.write(
        "#ifdef SWIGPYTHON\n"
        "%%apply void *PY_AS_VOID {\n"
        "  void *%s\n"
        "};\n"
        "#endif\n" % ( ",\n  void *".join(batons) )
      )


  def _write_callbacks(self, callbacks):
    """Write invoker functions for callbacks"""
    self.ofile.write('\n/* Callbacks */\n')
    self.ofile.write("\n%inline %{\n")

    struct = None
    for match in callbacks:
      if match[0] and not match[1]:
        # Struct definitions
        struct = match[0]
      elif not match[0] and struct not in self._ignores:
        # Struct member callbacks
        return_type, name, params = match[1:]
        type = "%s *" % struct

        self._write_callback(type, return_type, struct[:-2], name, params,
                             "(_obj->%s)" % name)
      elif match[0] and match[1]:
        # Callbacks declared as a typedef
        return_type, module, function, params = match
        type = "%s_%s_t" % (module, function)

        if type not in self._ignores:
          self._write_callback(type, return_type, module, function, params,
                               "_obj")

    self.ofile.write("%}\n")

    self.ofile.write("\n#ifdef SWIGPYTHON\n")
    for match in callbacks:

      if match[0] and not match[1]:
        # Struct definitions
        struct = match[0]
      elif not match[0] and struct not in self._ignores:
        # Using funcptr_member_proxy, add proxy methods to anonymous
        # struct member callbacks, so that they can be invoked directly.
        return_type, name, params = match[1:]
        self.ofile.write('%%funcptr_member_proxy(%s, %s, %s_invoke_%s);\n'
          % (struct, name, struct[:-2], name))
      elif match[0] and match[1]:
        # Using funcptr_proxy, create wrapper objects for each typedef'd
        # callback, so that they can be invoked directly. The
        # CALLABLE_CALLBACK typemap (used in _write_callback_typemaps)
        # ensures that these wrapper objects are actually used.
        return_type, module, function, params = match
        self.ofile.write('%%funcptr_proxy(%s_%s_t, %s_invoke_%s);\n'
          % (module, function, module, function))
    self.ofile.write("\n#endif\n")

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

  """Regular expression for parsing callbacks declared inside structs
     from a C header file"""
  _re_struct_callbacks = re.compile(r'\btypedef\s+(?:struct|union)\s+'
                                    r'(svn_[a-z_0-9]+)\b|'
                                    r'\n[ \t]+((?!typedef)[a-z_0-9\s*]+)'
                                    r'\(\*(\w+)\)'
                                    r'\s*\(([^)]+)\);')


  """Regular expression for parsing callbacks declared as a typedef
     from a C header file"""
  _re_typed_callbacks = re.compile(r'typedef\s+([a-z_0-9\s*]+)'
                                   r'\(\*(svn_[a-z]+)_([a-z_0-9]+)_t\)\s*'
                                   r'\(([^)]+)\);');

  """Regular expression for parsing batons"""
  _re_batons = re.compile(r'void\s*\*\s*(\w*baton\w*)');

  """Regular expression for parsing parameter names from a parameter list"""
  _re_param_names = re.compile(r'\b(\w+)\s*\)*\s*(?:,|$)')

  """Regular expression for parsing comments"""
  _re_comments = re.compile(r'/\*.*?\*/')

  def _write_swig_interface_file(self, base_fname, batons, includes, structs,
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

    # Write typemaps for the callbacks
    self._write_callback_typemaps(callbacks)

    # Write typemaps for the batons
    self._write_baton_typemaps(batons)

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

    # Get list of batons
    batons = unique(self._re_batons.findall(contents))

    # Get list of callbacks
    callbacks = (self._re_struct_callbacks.findall(contents) +
                 self._re_typed_callbacks.findall(contents))

    # Get the location of the output file
    base_fname = os.path.basename(fname)

    # Write the SWIG interface file
    self._write_swig_interface_file(base_fname, batons, includes, structs,
                                    callbacks)

  def write(self):
    """Generate wrappers for all header files"""

    for fname in self.header_files:
      self.process_header_file(fname)

if __name__ == "__main__":
  if len(sys.argv) < 3:
    print("""Usage: %s build.conf swig [ subversion/include/header_file.h ]
Generates SWIG proxy wrappers around Subversion header files. If no header
files are specified, generate wrappers for subversion/include/*.h. """ % \
    os.path.basename(sys.argv[0]))
  else:
    gen = Generator(sys.argv[1], sys.argv[2])
    if len(sys.argv) > 3:
      for fname in sys.argv[3:]:
        gen.process_header_file(fname)
    else:
      gen.write()
