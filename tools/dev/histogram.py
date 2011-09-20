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

import sys


def count(infile):
  counts = {}
  for line in infile:
    line = line.strip()
    if line in counts:
      counts[line] = counts[line] + 1
    else:
      counts[line] = 1

  return counts


def histogram(counts, width):
  (max_len, max_count) = reduce(lambda x, i: (max(len(i[0]), x[0]),
                                              max(i[1], x[1])),
                                counts.iteritems(), (0, 0))

  adjustor = float(max_count) / (width - max_len - 3)

  for (count, author) in sorted((v, k) for (k, v) in counts.items())[::-1]:
    print "%s%s | %s" % (author, " "*(max_len-len(author)),
                                 "X"*int(count/adjustor))


if __name__ == '__main__':
  if len(sys.argv) < 2:
    ### TODO: Automagically determine terminal width
    width = 80
  else:
    width = int(sys.argv[1])
  counts = count(sys.stdin)

  histogram(counts, width)
