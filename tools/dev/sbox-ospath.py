#!/usr/bin/env python
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
# USAGE:
#   $ ./sbox-ospath.py FILENAME
#
# This script will look for all lines in the file that use an expression
# that looks like:
#    os.path.join(wc_dir, 'A', 'B')
#
# and rewrite that to:
#    sbox.ospath('A/B')
#
# Obviously, this relies heavily on standard naming for the variables in
# our testing code. Visual inspection (and execution!) should be performed.
#
# The file is rewritten in place.
#

import sys
import os
import re

RE_FIND_JOIN = re.compile(r'os\.path\.join\((?:sbox\.)?wc_dir, '
                          r'(["\'][^"\']*["\'](?:, ["\'][^"\']*["\'])*)\)')


def rewrite_file(fname):
  count = 0
  lines = open(fname).readlines()
  for i in range(len(lines)):
    line = lines[i]
    match = RE_FIND_JOIN.search(line)
    if match:
      start, end = match.span()
      parts = match.group(1).replace('"', "'").replace("', '", '/')
      lines[i] = line[:start] + 'sbox.ospath(' + parts + ')' + line[end:]
      count += 1
  if count == 0:
    print('No changes.')
  else:
    open(fname, 'w').writelines(lines)
    print('%s rewrites performed.' % (count,))


if __name__ == '__main__':
  rewrite_file(sys.argv[1])
