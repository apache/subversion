import os
from build.generator.gen_make import UnknownDependency
import ezt
import gen_base

class _eztdata(object):
  def __init__(self, **kw):
    vars(self).update(kw)

class Generator(gen_base.GeneratorBase):
  _extension_map = {
    ('exe', 'target'): '.exe',
    ('exe', 'object'): '.obj',
    ('lib', 'target'): '.dll',
    ('lib', 'object'): '.obj',
    ('pyd', 'target'): '.pyd',
    ('pyd', 'object'): '.obj',
    ('so', 'target'): '.so',
    ('so', 'object'): '.obj',
  }

  def __init__(self, fname, verfname, options=None):
    gen_base.GeneratorBase.__init__(self, fname, verfname, options)

  def write(self):
    data = _eztdata(
      targets = "targets",
    )

    template = ezt.Template(os.path.join('build', 'generator', 'templates',
                                         'CMakeLists.txt.ezt'),
                            compress_whitespace=False)
    template.generate(open('CMakeLists.txt', 'w'), data)
