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


# This module writes a sequence of 'svn' commands to a file, that when
# run will perform the branching and merging described by a given MergeDot
# graph description object.


def shebang_line(out):
  out.write('#!/bin/sh\n')

def command(out, cmd, *args):
  """Write the shell command CMD with the arguments ARGS to the file-like
     object OUT.
  """
  out.write(' '.join((cmd,) + args) + "\n")

def svn(out, subcmd, *args):
  """Write an svn command with the given subcommand and arguments.  Write
     to the file-like object OUT.
  """
  command(out, 'svn', subcmd, *args)

def comment(out, text):
  """Write the comment TEXT to the file-like object OUT.
  """
  out.write('# %s\n' % text)

def node_branch(node_name):
  """Extract branch name from a node name.
     ### TODO: multi-char names.
  """
  return node_name[:1]

def node_url(node_name):
  """Extract the URL (in command-line repo-relative URL syntax) from a
     node name.
  """
  return '^/' + node_branch(node_name)

def node_rev(node_name):
  """Extract revnum (as an integer) from a node name.
     ### TODO: multi-char names.
  """
  return int(node_name[1:]) + 1

def add(revs, node_name, action, *args):
  """Add the tuple (ACTION, (ARGS)) to the list REVS[REVNUM].
  """
  revnum = node_rev(node_name)
  if not revnum in revs:
    revs[revnum] = []
  revs[revnum].append((action, args))

def write_recipe(graph, out):
  """Write out a sequence of svn commands that will execute the branching
     and merging shown in GRAPH.  Write to the file-like object OUT.
  """
  revs = {}  # keyed by revnum

  for br, orig, r1, head in graph.branches:
    if orig:
      add(revs, br + str(r1), 'copy', orig, br)
    else:
      add(revs, br + str(r1), 'mkproj', br)

  for base_node, src_node, tgt_node, kind in graph.merges:
    add(revs, tgt_node, 'merge', src_node, tgt_node, kind)

  for node_name in graph.changes:
    # Originally the 'changes' list could have entries that overlapped with
    # merges. We must either disallow that or filter out such changes here.
    #if not node_name in revs:
    add(revs, node_name, 'modify', node_name)

  # Execute the actions for each revision in turn.
  for r in sorted(revs.keys()):
    comment(out, 'start r' + str(r))
    for action, params in revs[r]:
      #comment(out, '(' + action + ' ' + params + ')')
      if action == 'mkproj':
        (br,) = params
        svn(out, 'mkdir', br, br + '/created_in_' + br)
      elif action == 'copy':
        (orig, br) = params
        svn(out, 'copy', '-r' + str(node_rev(orig)), node_branch(orig), br)
      elif action == 'modify':
        (node_name,) = params
        svn(out, 'mkdir', node_branch(node_name) + '/new_in_' + node_name)
      elif action == 'merge':
        (src_node, tgt_node, kind) = params
        assert node_rev(tgt_node) == r
        svn(out, 'update')
        if kind == 'cherry':
          svn(out, 'merge',
              '-c' + str(node_rev(src_node)), node_url(src_node),
              node_branch(tgt_node))
        elif kind.startswith('reint'):
          svn(out, 'merge', '--reintegrate',
              node_url(src_node) + '@' + str(node_rev(src_node)),
              node_branch(tgt_node))
        else:
          svn(out, 'merge',
              node_url(src_node) + '@' + str(node_rev(src_node)),
              node_branch(tgt_node))
      else:
        raise Exception('unknown action: %s' % action)
    svn(out, 'commit', '-m', 'r' + str(r))

def write_sh_file(graph, filename):
  """Write a file containing a sequence of 'svn' commands that when run will
     perform the branching and merging described by the MergeDot object
     GRAPH.  Write to a new file named FILENAME.
  """
  out_stream = open(filename, 'w')
  shebang_line(out_stream)
  write_recipe(graph, out_stream)
  out_stream.close()
