#
# generator.swig: Base class for SWIG-related generators
#

import shutil, ConfigParser, re, os
import generator.util.executable
_exec = generator.util.executable

class Generator:
  """Base class for SWIG-related generators"""
  langs = ["python", "perl", "ruby"]
  short = { "perl": "pl", "python": "py", "ruby": "rb" }

  def __init__(self, conf, swig_path):
    """Read build.conf"""

    # Now read and parse build.conf
    parser = ConfigParser.ConfigParser()
    parser.read(conf)

    # Read configuration options
    self.proxy_dir = parser.get('options', 'swig-proxy-dir')
    self.include_dirs = parser.get('options','include-dirs')
    self.swig_include_dirs = parser.get('options','swig-include-dirs')

    # Calculate build options
    self.opts = {}
    for lang in self.langs:
      self.opts[lang] = parser.get('options', 'swig-%s-opts' % lang)

    # Calculate SWIG paths
    self.swig_path = swig_path
    try:
      self.swig_libdir = _exec.output("%s -swiglib" % self.swig_path, strip=1)
    except AssertionError:
      pass

  def version(self):
    """Get the version number of SWIG"""
    try:
      swig_version = _exec.output("%s -version" % self.swig_path)
      m = re.search("Version (\d+).(\d+).(\d+)", swig_version)
      if m:
        return int(
          "%s0%s0%s" % (m.group(1), m.group(2), m.group(3)))
    except AssertionError:
      pass
    return 0

  def checkout(self, dir, file):
    """Checkout a specific header file from SWIG"""
    out = "%s/%s" % (self.proxy_dir, file)
    if os.path.exists(out):
      os.remove(out)
    if self.version() == 103024:
      shutil.copy("%s/%s/%s" % (self.swig_libdir, dir, file), out)
    else:
      _exec.run("%s -o %s -co %s/%s" % (self.swig_path, out, dir, file))

