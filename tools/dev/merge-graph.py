#!/usr/bin/env python

# ====================================================================
#    Licensed to the Apache Software Foundation (ASF) under one
#    or more contributor license agreements.  See the NOTICE file
#    distributed with this work for additional information
#    regarding copyright ownership.  The ASF licenses this file
#    to you under the Apache License, Version 2.0 (the
#    "License"); you may not use this file except in compliance
#    with the License.  You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
#    Unless required by applicable law or agreed to in writing,
#    software distributed under the License is distributed on an
#    "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
#    KIND, either express or implied.  See the License for the
#    specific language governing permissions and limitations
#    under the License.
# ====================================================================

args_message = '[-f png|svg|gif|dia... [-f ...]] GRAPH_CONFIG_FILE...'
help_message = """Produce pretty graphs representing branches and merging.
For each config file specified, construct a graph and write it as a PNG file
(or other graphical file formats)."""

import sys
import getopt
from mergegraph import MergeDot


# If run as a program, process each input filename as a graph config file.
if __name__ == '__main__':
  optlist, args = getopt.getopt(sys.argv[1:], 'f:', ['format'])

  prog_name = sys.argv[0]
  if not args:
    usage = '%s: usage: "%s %s"\n' % (prog_name, prog_name, args_message)
    sys.stderr.write(usage)
    sys.exit(1)

  formats = []

  for opt, opt_arg in optlist:
    if opt == '-f':
      formats.append(opt_arg)

  if not formats:
    formats.append('png')

  for config_filename in args:
    sys.stdout.write("%s: reading '%s', " % (prog_name, config_filename))
    graph = MergeDot(config_filename, rankdir='LR', dpi='72')
    for format in formats:
      filename = '%s.%s' % (graph.basename, format)
      sys.stdout.write("writing '%s' " % filename)
      graph.save(format=format, filename=filename)
    print
