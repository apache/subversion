#
# generator.swig: Base class for SWIG-related generators
#

import shutil, ConfigParser, re, os
import generator.util.executable as _exec
from generator.gen_base import _collect_paths

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
    self.includes = _collect_paths(parser.get('options', 'includes'))
    self.swig_checkout_files = \
      _collect_paths(parser.get('options', 'swig-checkout-files'))

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

