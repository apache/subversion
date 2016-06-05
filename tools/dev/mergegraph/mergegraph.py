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

# Config file format:
example = """
  [graph]
  filename = merge-sync-1.png
  title = Sync Merge: CC vs SVN
  # Branches: (branch name, branched from node, first rev, last rev).
  branches = [
    ('A', 'O0', 1, 4),
    ('O', None, 0, 0),
    ('B', 'O0', 1, 5)
    ]
  # Changes: nodes in which a change was committed; merge targets need not
  # be listed here.
  changes = [
    'A1', 'A2', 'A3', 'A4',
    'B1', 'B2', 'B3', 'B4', 'B5'
    ]
  # Merges: (base node, source-right node, target node, label).
  # Base is also known as source-left.
  merges = [
    ('O0', 'A:1', 'B3', 'sync'),
    ('A2', 'A:3', 'B5', 'sync'),
    ]
  # Annotations for nodes: (node, annotation text).
  annotations = [
    ('A2', 'cc:YCA')
    ]
"""

# Notes about different kinds of merge.
#
# A basic 3-way merge is ...
#
# The ClearCase style of merge is a 3-way merge.
#
# The Subversion style of merge (that is, one phase of a Subversion merge)
# is a three-way merge with its base (typically the YCA) on the source branch.


import sys
import pydot
from pydot import Node, Edge


def mergeinfo_to_node_list(mi):
  """Convert a mergeinfo string such as '/foo:1,3-5*' into a list of
     node names such as ['foo1', 'foo3', 'foo4', 'foo5'].
  """
  ### Doesn't yet strip the leading slash.
  l = []
  if mi:
    for mi_str in mi.split(' '):
      path, ranges = mi_str.split(':')
      for r in ranges.split(','):
        if r.endswith('*'):
          # TODO: store & use this 'non-inheritable' flag
          # Remove the flag
          r = r[:-1]
        rlist = r.split('-')
        r1 = int(rlist[0])
        if len(rlist) == 2:
          r2 = int(rlist[1])
        else:
          r2 = r1
        for rev in range(r1, r2 + 1):
          l.append(path + str(rev))
  return l


class MergeGraph(pydot.Graph):
  """Base class, not intended for direct use.  Use MergeDot for the main
     graph and MergeSubgraph for a subgraph.
  """

  def mk_origin_node(graph, name, label):
    """Add a node to the graph"""
    graph.add_node(Node(name, label=label, shape='plaintext'))

  def mk_invis_node(graph, name):
    """Add a node to the graph"""
    graph.add_node(Node(name, style='invis'))

  def mk_node(graph, name, label=None):
    """Add a node to the graph, if not already present"""
    if not graph.get_node(name):
      if not label:
        label = name
      if name in graph.changes:
        graph.add_node(Node(name, label=label))
      else:
        graph.add_node(Node(name, color='grey', label=''))

  def mk_merge_target(graph, target_node, important):
    """Add a merge target node to the graph."""
    if important:
      color = 'red'
    else:
      color = 'black'
    graph.add_node(Node(target_node, color=color, fontcolor=color, style='bold'))

  def mk_edge(graph, name1, name2, **attrs):
    """Add an ordinary edge to the graph"""
    graph.add_edge(Edge(name1, name2, dir='none', style='dotted', color='grey', **attrs))

  def mk_br_edge(graph, name1, name2):
    """Add a branch-creation edge to the graph"""
    # Constraint=false to avoid the Y-shape skewing the nice parallel branch lines
    graph.mk_edge(name1, name2, constraint='false')

  def mk_merge_edge(graph, src_node, tgt_node, kind, label, important):
    """Add a merge edge to the graph"""
    if important:
      color = 'red'
    else:
      color = 'grey'
    e = Edge(src_node, tgt_node, constraint='false',
             label='"' + label + '"',
             color=color, fontcolor=color,
             style='bold')
    if kind.startswith('cherry'):
      e.set_style('dashed')
    graph.add_edge(e)

  def mk_mergeinfo_edge(graph, base_node, src_node, important):
    """"""
    if important:
      color = 'red'
    else:
      color = 'grey'
    graph.add_edge(Edge(base_node, src_node,
                        dir='both', arrowtail='odot', arrowhead='tee',
                        color=color, constraint='false'))

  def mk_invis_edge(graph, name1, name2):
    """Add an invisible edge to the graph"""
    graph.add_edge(Edge(name1, name2, style='invis'))

  def add_merge(graph, merge, important):
    """Add a merge"""
    base_node, src_node, tgt_node, kind = merge

    if base_node and src_node:  # and not kind.startwith('cherry'):
      graph.mk_mergeinfo_edge(base_node, src_node, important)

    # Merge target node
    graph.mk_merge_target(tgt_node, important)

    # Merge edge
    graph.mk_merge_edge(src_node, tgt_node, kind, kind, important)

  def add_annotation(graph, node, label, color='lightblue'):
    """Add a graph node that serves as an annotation to a normal node.
       More than one annotation can be added to the same normal node.
    """
    subg_name = node + '_annotations'

    def get_subgraph(graph, name):
      """Equivalent to pydot.Graph.get_subgraph() when there is no more than
         one subgraph of the given name, but working aroung a bug in
         pydot.Graph.get_subgraph().
      """
      for subg in graph.get_subgraph_list():
        if subg.get_name() == name:
          return subg
      return None

    g = get_subgraph(graph, subg_name)
    if not g:
      g = pydot.Subgraph(subg_name, rank='same')
      graph.add_subgraph(g)

    ann_node = node + '_'
    while g.get_node(ann_node):
      ann_node = ann_node + '_'
    g.add_node(Node(ann_node, shape='box', style='filled', color=color,
                    label='"' + label + '"'))
    g.add_edge(Edge(ann_node, node, style='solid', color=color,
                    dir='none', constraint='false'))

class MergeSubgraph(MergeGraph, pydot.Subgraph):
  """"""
  def __init__(graph, **attrs):
    """"""
    MergeGraph.__init__(graph)
    pydot.Subgraph.__init__(graph, **attrs)

class MergeDot(MergeGraph, pydot.Dot):
  """
  # TODO: In the 'merges' input, find the predecessor automatically.
  """
  def __init__(graph, config_filename=None,
               filename=None, title=None, branches=None, changes=None,
               merges=[], annotations=[], **attrs):
    """Return a new MergeDot graph generated from a config file or args."""
    MergeGraph.__init__(graph)
    pydot.Dot.__init__(graph, **attrs)

    if config_filename:
      graph.read_config(config_filename)
    else:
      graph.filename = filename
      graph.title = title
      graph.branches = branches
      graph.changes = changes
      graph.merges = merges
      graph.annotations = annotations

    graph.construct()

  def read_config(graph, config_filename):
    """Initialize a MergeDot graph's input data from a config file."""
    import ConfigParser
    if config_filename.endswith('.txt'):
      default_basename = config_filename[:-4]
    else:
      default_basename = config_filename

    config = ConfigParser.SafeConfigParser({ 'basename': default_basename,
                                             'title': None,
                                             'merges': '[]',
                                             'annotations': '[]' })
    files_read = config.read(config_filename)
    if len(files_read) == 0:
      sys.stderr.write('graph: unable to read graph config from "' + config_filename + '"\n')
      sys.exit(1)
    graph.basename = config.get('graph', 'basename')
    graph.title = config.get('graph', 'title')
    graph.branches = eval(config.get('graph', 'branches'))
    graph.changes = eval(config.get('graph', 'changes'))
    graph.merges = eval(config.get('graph', 'merges'))
    graph.annotations = eval(config.get('graph', 'annotations'))

  def construct(graph):
    """"""
    # Origin nodes (done first, in an attempt to set the order)
    for br, orig, r1, head in graph.branches:
      name = br + '0'
      if r1 > 0:
        graph.mk_origin_node(name, br)
      else:
        graph.mk_node(name, label=br)

    # Edges and target nodes for merges
    for merge in graph.merges:
      # Emphasize the last merge, as it's the important one
      important = (merge == graph.merges[-1])
      graph.add_merge(merge, important)

    # Parallel edges for basic lines of descent
    for br, orig, r1, head in graph.branches:
      sub_g = MergeSubgraph(ordering='out')
      for i in range(1, head + 1):
        prev_n = br + str(i - 1)
        this_n = br + str(i)

        # Normal edges and nodes
        if i < r1:
          graph.mk_invis_node(this_n)
        else:
          graph.mk_node(this_n)
        if i <= r1:
          graph.mk_invis_edge(prev_n, this_n)
        else:
          graph.mk_edge(prev_n, this_n)

      # Branch creation edges
      if orig:
        sub_g.mk_br_edge(orig, br + str(r1))

      graph.add_subgraph(sub_g)

    # Annotations
    for node, label in graph.annotations:
      graph.add_annotation(node, label)

    # A title for the graph (added last so it goes at the top)
    if graph.title:
      graph.add_node(Node('title', shape='plaintext', label='"' + graph.title + '"'))

  def save(graph, format='png', filename=None):
    """Save this merge graph to the given file format. If filename is None,
       construct a filename from the basename of the original file (as passed
       to the constructor and then stored in graph.basename) and the suffix
       according to the given format.
    """
    if not filename:
      filename = graph.basename + '.' + format
    if format == 'sh':
      import save_as_sh
      save_as_sh.write_sh_file(graph, filename)
    else:
      pydot.Dot.write(graph, filename, format=format)
