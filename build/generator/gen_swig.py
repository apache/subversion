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
  _ignores = ["svn_md5.h", "svn_repos_parse_fns_t"]

  def __init__(self, fname, verfname, options=None):
    gen_base.GeneratorBase.__init__(self, fname, verfname, options)
    self.swig_path = self._find_executable("swig") or "swig"
    self.swig_version = self._get_swig_version()
    self.apr_include_path = self._get_apr_include_path()
    assert self.apr_include_path, "Cannot find APR include path"
    assert os.path.isdir(self.apr_include_path), "Cannot find APR include path"

  def _is_executable(self, file):
    """Is this an executable file?"""
    return os.path.isfile(file) and os.access(file, os.X_OK)

  def _find_executable(self, file, dirs=None):
    """Search for a specified file in a given list of directories.
       If no directories are given, search according to the PATH
       environment variable."""
    if not dirs:
      dirs = string.split(os.environ["PATH"], os.pathsep)
    for path in dirs:
      if self._is_executable(os.path.join(path, file)):
        return os.path.join(path, file)
    return None

  def _get_swig_version(self):
    """Get the version number of SWIG"""
    swig_version_cmd = "sh -c '%s -version 2>&1'" % self.swig_path
    swig_version = os.popen(swig_version_cmd).read()
    m = re.search("Version (\d+).(\d+).(\d+)", swig_version)
    if m:
      return int(
        "%s0%s0%s" % (m.group(1), m.group(2), m.group(3)))
    else:
      return 0

  def _get_apr_include_path(self):
    """Get the APR include directory"""
    if os.path.isdir(os.path.join("apr","include")):
      return os.path.join("apr","include")
    else:
      # Search for APR using the "apr-config" executable
      apr_config_path = self._find_executable("apr-config")
      if apr_config_path:
        apr_version_cmd = "sh -c '%s --includedir 2>&1'" % apr_config_path
        return string.strip(os.popen(apr_version_cmd).read())

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
  _re_structs = re.compile(r'\btypedef\s+struct\s+(svn_[a-z_0-9]+_t)\b\s*(\{?)')

  """Regular expression for parsing callbacks from a C header file"""
  _re_callbacks = re.compile(r'\btypedef\s+struct\s+(svn_[a-z_0-9]+_t)\b|'
                             r'\n\s*svn_error_t\s*\*\(\*(\w+)\)\s*\(([^)]+)\);')
  
  """Regular expression for parsing parameter names from a parameter list"""
  _re_param_names = re.compile(r'\b(\w+)\s*\)*\s*(?:,|$)')
  
  """Regular expression for parsing comments"""
  _re_comments = re.compile(r'/\*.*?\*/')
  
  def _write_swig_interface_file(self, base_fname, includes, structs,
      callbacks):
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

    # Write callback definitions into the SWIG interface file
    self._write_callbacks(callbacks)

    # Close our output file
    self.ofile.close()

  def _process_header_file(self, fname):

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

    self._write_swig_interface_file(base_fname, includes, structs, callbacks)

  def _build_opts(self, lang):
    """Options to pass in to SWIG for a specified language"""
    return {
      "python": self.swig_python_opts,
      "ruby": self.swig_ruby_opts,
      "perl": self.swig_perl_opts
    }[lang]

  def _cmd(self, cmd):
    """Execute a system command"""
    return_code = os.system(cmd)
    assert return_code == 0
  
  def _checkout(self, dir, file):
    """Checkout a specific header file from SWIG"""
    out = "%s/%s" % (self.swig_proxy_dir, file)
    self._cmd("rm -f %s" % out)
    if self.swig_version == 103024:
      self._cmd("cp %s/%s/%s %s" % (self.swiglibdir, dir, file, out))
    else:
      self._cmd("%s -o %s -co %s/%s" % (self.swig_path, out, dir, file))
  
  def _write_external_runtime(self, langs):
    """Generate external runtime header files for each SWIG language"""
    
    # Gather necessary header files
    self.swiglibdir = string.strip(os.popen("%s -swiglib" %
        self.swig_path).read())
    assert self.swiglibdir
    self._checkout(".","swigrun.swg") 
    if self.swig_version == 103024:
      self._checkout(".","common.swg") 
    self._checkout(".","runtime.swg") 
    self._checkout("python","pyrun.swg") 
    self._checkout("perl5","perlrun.swg") 
    self._checkout("ruby","rubydef.swg") 
    self._checkout("ruby", "rubyhead.swg")
  
    # Runtime library names
    runtime_library = {
      "python": "pyrun.swg", "perl":"perlrun.swg", "ruby":"rubydef.swg"
    }
    
    # Build runtime files
    for lang in langs:
      out = "%s/swig_%s_external_runtime.swg" % (self.swig_proxy_dir, lang)
      if self.swig_version == 103024:
        self._cmd(
          "cp -f %s/swigrun.swg %s && " % (self.swig_proxy_dir, out) +
          "cat %s/common.swg >> %s && " % (self.swig_proxy_dir, out) +
          "cat %s/%s >> %s" % (self.swig_proxy_dir, runtime_library[lang], out)
        )
        if lang != "ruby":
          self._cmd("cat %s/runtime.swg >> %s" % (self.swig_proxy_dir, out))
      else:
        self._cmd("%s -%s -external-runtime %s" % (self.swig_path, lang, out))
  
  def _get_swig_includes(self):
    """Get list of SWIG includes as a string of compiler options"""
    includes = []
    for dirs in self.include_dirs, self.swig_include_dirs:
      for dir in string.split(string.strip(dirs)):
        includes.append("-I%s" % dir)
    includes.append("-I%s" % self.apr_include_path)
    includes.append("-Iapr-util/include")
    return string.join(includes)
  
  def _get_swig_deps(self):
    """Get list of SWIG dependencies"""
    deps = {"python":[], "perl":[], "ruby":[]}
    swig_c_deps = self.graph.get_deps(gen_base.DT_SWIG_C)
    swig_c_deps.sort(lambda (t1, s1), (t2, s2): cmp(t1.filename, t2.filename))
    for objname, sources in swig_c_deps:
      deps[objname.lang].append(str(objname))
    return deps
 
  def _write_swig_deps(self, langs):
    """Write SWIG dependencies to swig-outputs.mk"""
    
    # Gather data
    short = { "perl": "pl", "python": "py", "ruby": "rb" }
    includes = self._get_swig_includes() 
    deps = self._get_swig_deps()
   
    # Write swig-outputs.mk
    ofile = open("swig-outputs.mk", 'w')
    for lang in langs:
      ofile.write('RUN_SWIG_%s = %s -DSVN_SWIG_VERSION=%s %s %s -o $@\n'
        % (string.upper(short[lang]), self.swig_path, self.swig_version,
           self._build_opts(lang), includes))  
      ofile.write(
        'autogen-swig-%s: %s\n' % (short[lang], string.join(deps[lang])) +
        'swig-%s: autogen-swig-%s\n' % (short[lang], short[lang]) +
        'autogen-swig: autogen-swig-%s\n' % short[lang])
      if self.swig_version < 103024:
        ofile.write('%s: wrong-swig\n' % string.join(deps[lang]))
    if self.swig_version < 103024:
      ofile.write(
        'wrong-swig:\n'
        '\tpython -c "raise \'Cannot find SWIG 1.3.24 or better. '
        'Please install SWIG, add it to your PATH,\\n'
        'and rerun ./autogen.sh\'"')
    ofile.close()
  
  def write(self):
    """Generate SWIG interface files"""
    header_files = map(native_path, self.includes)
    self._include_basenames = map(os.path.basename, header_files)
    for fname in header_files:
      if fname[-2:] == ".h":
        self._process_header_file(fname)

    langs = ["perl", "python", "ruby"] 
    if self.swig_version >= 103024:
      self._write_external_runtime(langs)
    self._write_swig_deps(langs)
