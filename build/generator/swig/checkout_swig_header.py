#!/usr/bin/env python
#
#
# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing,
# software distributed under the License is distributed on an
# "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
# KIND, either express or implied.  See the License for the
# specific language governing permissions and limitations
# under the License.
#
#
#
# Checkout files from the SWIG library into Subversion's proxy directory
#

import sys, os, re, fileinput, shutil, subprocess
if __name__ == "__main__":
  parent_dir = os.path.dirname(os.path.abspath(os.path.dirname(sys.argv[0])))
  sys.path[0:0] = [ parent_dir, os.path.dirname(parent_dir) ]
import generator.swig
from gen_base import build_path_splitfile, build_path_join

class Generator(generator.swig.Generator):

  def write(self):
    """Checkout all files"""
    for path in self.swig_checkout_files:
      self.checkout(path)

  def write_makefile_rules(self, makefile):
    """Write makefile rules to checkout files"""
    script_path = '$(top_srcdir)/build/generator/swig/checkout_swig_header.py'
    conf = '$(abs_srcdir)/build.conf'
    makefile.write('CHECKOUT_SWIG = cd $(top_builddir) && $(PYTHON)' +
                   ' %s %s $(SWIG)\n\n' % (script_path, conf))
    checkout_locations = []
    for path in self.swig_checkout_files:
      out = self._output_file(path)
      checkout_locations.append(out)
      makefile.write('%s: %s\n' % (out, script_path) +
                     '\t$(CHECKOUT_SWIG) %s\n\n' % path)
    makefile.write('SWIG_CHECKOUT_FILES = %s\n\n\n'
                   % " ".join(checkout_locations))

  def checkout(self, path):
    """Checkout a specific header file from SWIG"""
    out = self._output_file(path)
    if os.path.exists(out):
      os.remove(out)
    if self._skip_checkout(path):
      open(out, "w")
    elif self.version() == (1, 3, 24):
      shutil.copy(build_path_join(self.swig_libdir, path), out)
    else:
      subprocess.check_call([self.swig_path, "-o", out, "-co", path])

  def _skip_checkout(self, path):
    """Should we skip this checkout?"""
    return (path == "ruby/rubytracking.swg" and self.version() < (1, 3, 26) or
            path == "common.swg" and self.version() > (1, 3, 24))

  def _output_file(self, path):
    """Get output filename"""
    dir, filename = build_path_splitfile(path)
    return build_path_join(self.proxy_dir, filename)

if __name__ == "__main__":
  if len(sys.argv) != 4:
    print("Usage: %s build.conf swig file.swg")
    print("Checkout a specific header file from SWIG's library into")
    print("the Subversion proxy directory.")
  else:
    gen = Generator(sys.argv[1], sys.argv[2])
    gen.checkout(sys.argv[3])
