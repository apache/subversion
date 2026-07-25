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
import operator


def count(infile):
  # infile should be a simple file with author names on each line
  counts = {}
  for line in infile:
    author = line.strip()
    counts[author] = counts.get(author, 0) + 1

  return counts


def histogram(counts, width):
  max_len = max([len(author) for author in counts.keys()])
  max_count = max(counts.values())

  adjustor = float(max_count) / (width - max_len - 3)

  for author, count in sorted(counts.items(),
                              key=operator.itemgetter(1),  # sort on count
                              reverse=True):
    print("%-*s | %s" % (max_len, author, "X"*int(count/adjustor)))


if __name__ == '__main__':
  if len(sys.argv) < 2:
    ### TODO: Automagically determine terminal width
    width = 80
  else:
    width = int(sys.argv[1])
  histogram(count(sys.stdin), width)
