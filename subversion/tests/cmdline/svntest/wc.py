#
#  wc.py: functions for interacting with a Subversion working copy
#
#  Subversion is a tool for revision control.
#  See http://subversion.tigris.org for more information.
#
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
######################################################################

import os
import sys
import re
import logging
import pprint

if sys.version_info[0] >= 3:
  # Python >=3.0
  from io import StringIO
  from urllib.parse import quote as urllib_quote
else:
  # Python <3.0
  from cStringIO import StringIO
  from urllib import quote as urllib_quote

import svntest

logger = logging.getLogger()


#
# 'status -v' output looks like this:
#
#      "%c%c%c%c%c%c%c %c   %6s   %6s %-12s %s\n"
#
# (Taken from 'print_status' in subversion/svn/status.c.)
#
# Here are the parameters.  The middle number or string in parens is the
# match.group(), followed by a brief description of the field:
#
#    - text status           (1)  (single letter)
#    - prop status           (1)  (single letter)
#    - wc-lockedness flag    (2)  (single letter: "L" or " ")
#    - copied flag           (3)  (single letter: "+" or " ")
#    - switched flag         (4)  (single letter: "S", "X" or " ")
#    - repos lock status     (5)  (single letter: "K", "O", "B", "T", " ")
#    - tree conflict flag    (6)  (single letter: "C" or " ")
#
#    [one space]
#
#    - out-of-date flag      (7)  (single letter: "*" or " ")
#
#    [three spaces]
#
#    - working revision ('wc_rev') (either digits or "-", "?" or " ")
#
#    [one space]
#
#    - last-changed revision      (either digits or "?" or " ")
#
#    [one space]
#
#    - last author                (optional string of non-whitespace
#                                  characters)
#
#    [spaces]
#
#    - path              ('path') (string of characters until newline)
#
# Working revision, last-changed revision, and last author are whitespace
# only if the item is missing.
#
_re_parse_status = re.compile('^([?!MACDRUGXI_~ ][MACDRUG_ ])'
                              '([L ])'
                              '([+ ])'
                              '([SX ])'
                              '([KOBT ])'
                              '([C ]) '
                              '([* ]) +'
                              '((?P<wc_rev>\d+|-|\?) +(\d|-|\?)+ +(\S+) +)?'
                              '(?P<path>.+)$')

_re_parse_status_ex = re.compile('^      ('
               '(  \> moved (from (?P<moved_from>.+)|to (?P<moved_to>.*)))'
              '|(  \> swapped places with (?P<swapped_with>.+).*)'
              '|(\>   (?P<tc>.+))'
  ')$')

_re_parse_skipped = re.compile("^(Skipped[^']*) '(.+)'( --.*)?\n")

_re_parse_summarize = re.compile("^([MAD ][M ])      (.+)\n")

_re_parse_checkout = re.compile('^([RMAGCUDE_ B][MAGCUDE_ ])'
                                '([B ])'
                                '([CAUD ])\s+'
                                '(.+)')
_re_parse_co_skipped = re.compile('^(Restored|Skipped|Removed external)'
                                  '\s+\'(.+)\'(( --|: ).*)?')
_re_parse_co_restored = re.compile('^(Restored)\s+\'(.+)\'')

# Lines typically have a verb followed by whitespace then a path.
_re_parse_commit_ext = re.compile('^(([A-Za-z]+( [a-z]+)*)) \'(.+)\'( --.*)?')
_re_parse_commit = re.compile('^(\w+(  \(bin\))?)\s+(.+)')

#rN: eids 0 15 branches 4
_re_parse_eid_header = re.compile('^r(-1|[0-9]+): eids ([0-9]+) ([0-9]+) '
                                  'branches ([0-9]+)$')
# B0.2 root-eid 3
_re_parse_eid_branch = re.compile('^(B[0-9.]+) root-eid ([0-9]+) num-eids ([0-9]+)( from [^ ]*)?$')
_re_parse_eid_merge_history = re.compile('merge-history: merge-ancestors ([0-9]+)')
# e4: normal 6 C
_re_parse_eid_ele = re.compile('^e([0-9]+): (none|normal|subbranch) '
                               '(-1|[0-9]+) (.*)$')

class State:
  """Describes an existing or expected state of a working copy.

  The primary metaphor here is a dictionary of paths mapping to instances
  of StateItem, which describe each item in a working copy.

  Note: the paths should be *relative* to the root of the working copy,
  using '/' for the separator (see to_relpath()), and the root of the
  working copy is identified by the empty path: ''.
  """

  def __init__(self, wc_dir, desc):
    "Create a State using the specified description."
    assert isinstance(desc, dict)

    self.wc_dir = wc_dir
    self.desc = desc      # dictionary: path -> StateItem

  def add(self, more_desc):
    "Add more state items into the State."
    assert isinstance(more_desc, dict)

    self.desc.update(more_desc)

  def add_state(self, parent, state, strict=False):
    "Import state items from a State object, reparent the items to PARENT."
    assert isinstance(state, State)

    for path, item in state.desc.items():
      if strict:
        path = parent + path
      elif path == '':
        path = parent
      else:
        path = parent + '/' + path
      self.desc[path] = item

  def remove(self, *paths):
    "Remove PATHS from the state (the paths must exist)."
    for path in paths:
      del self.desc[to_relpath(path)]

  def remove_subtree(self, *paths):
    "Remove PATHS recursively from the state (the paths must exist)."
    for subtree_path in paths:
      subtree_path = to_relpath(subtree_path)
      for path, item in svntest.main.ensure_list(self.desc.items()):
        if path == subtree_path or path[:len(subtree_path) + 1] == subtree_path + '/':
          del self.desc[path]

  def copy(self, new_root=None):
    """Make a deep copy of self.  If NEW_ROOT is not None, then set the
    copy's wc_dir NEW_ROOT instead of to self's wc_dir."""
    desc = { }
    for path, item in self.desc.items():
      desc[path] = item.copy()
    if new_root is None:
      new_root = self.wc_dir
    return State(new_root, desc)

  def tweak(self, *args, **kw):
    """Tweak the items' values.

    Each argument in ARGS is the path of a StateItem that already exists in
    this State. Each keyword argument in KW is a modifiable property of
    StateItem.

    The general form of this method is .tweak([paths...,] key=value...). If
    one or more paths are provided, then those items' values are
    modified.  If no paths are given, then all items are modified.
    """
    if args:
      for path in args:
        try:
          path_ref = self.desc[to_relpath(path)]
        except KeyError as e:
          e.args = ["Path '%s' not present in WC state descriptor" % path]
          raise
        path_ref.tweak(**kw)
    else:
      for item in self.desc.values():
        item.tweak(**kw)

  def tweak_some(self, filter, **kw):
    "Tweak the items for which the filter returns true."
    for path, item in self.desc.items():
      if list(filter(path, item)):
        item.tweak(**kw)

  def rename(self, moves):
    """Change the path of some items.

    MOVES is a dictionary mapping source path to destination
    path. Children move with moved parents.  All subtrees are moved in
    reverse depth order to temporary storage before being moved in
    depth order to the final location.  This allows nested moves.

    """
    temp = {}
    for src, dst in sorted(moves.items(), key=lambda pair: pair[0])[::-1]:
      temp[src] = {}
      for path, item in svntest.main.ensure_list(self.desc.items()):
        if path == src or path[:len(src) + 1] == src + '/':
          temp[src][path] = item;
          del self.desc[path]
    for src, dst in sorted(moves.items(), key=lambda pair: pair[1]):
      for path, item in temp[src].items():
        if path == src:
          new_path = dst
        else:
          new_path = dst + path[len(src):]
        self.desc[new_path] = item

  def subtree(self, subtree_path):
    """Return a State object which is a deep copy of the sub-tree
    beneath SUBTREE_PATH (which is assumed to be rooted at the tree of
    this State object's WC_DIR).  Exclude SUBTREE_PATH itself."""
    desc = { }
    for path, item in self.desc.items():
      if path[:len(subtree_path) + 1] == subtree_path + '/':
        desc[path[len(subtree_path) + 1:]] = item.copy()
    return State(self.wc_dir, desc)

  def write_to_disk(self, target_dir):
    """Construct a directory structure on disk, matching our state.

    WARNING: any StateItem that does not have contents (.contents is None)
    is assumed to be a directory.
    """
    if not os.path.exists(target_dir):
      os.makedirs(target_dir)

    for path, item in self.desc.items():
      fullpath = os.path.join(target_dir, path)
      if item.contents is None:
        # a directory
        if not os.path.exists(fullpath):
          os.makedirs(fullpath)
      else:
        # a file

        # ensure its directory exists
        dirpath = os.path.dirname(fullpath)
        if not os.path.exists(dirpath):
          os.makedirs(dirpath)

        # write out the file contents now
        svntest.main.file_write(fullpath, item.contents, 'wb')

  def normalize(self):
    """Return a "normalized" version of self.

    A normalized version has the following characteristics:

      * wc_dir == ''
      * paths use forward slashes
      * paths are relative

    If self is already normalized, then it is returned. Otherwise, a
    new State is constructed with (shallow) references to self's
    StateItem instances.

    If the caller needs a fully disjoint State, then use .copy() on
    the result.
    """
    if self.wc_dir == '':
      return self

    base = to_relpath(os.path.normpath(self.wc_dir))

    desc = dict([(repos_join(base, path), item)
                 for path, item in self.desc.items()])

    for path, item in desc.copy().items():
      if item.moved_from or item.moved_to:
        i = item.copy()

        if i.moved_from:
          i.moved_from = to_relpath(os.path.normpath(
                                        repos_join(base, i.moved_from)))
        if i.moved_to:
          i.moved_to = to_relpath(os.path.normpath(
                                        repos_join(base, i.moved_to)))

        desc[path] = i

    return State('', desc)

  def compare(self, other):
    """Compare this State against an OTHER State.

    Three new set objects will be returned: CHANGED, UNIQUE_SELF, and
    UNIQUE_OTHER. These contain paths of StateItems that are different
    between SELF and OTHER, paths of items unique to SELF, and paths
    of item that are unique to OTHER, respectively.
    """
    assert isinstance(other, State)

    norm_self = self.normalize()
    norm_other = other.normalize()

    # fast-path the easy case
    if norm_self == norm_other:
      fs = frozenset()
      return fs, fs, fs

    paths_self = set(norm_self.desc.keys())
    paths_other = set(norm_other.desc.keys())
    changed = set()
    for path in paths_self.intersection(paths_other):
      if norm_self.desc[path] != norm_other.desc[path]:
        changed.add(path)

    return changed, paths_self - paths_other, paths_other - paths_self

  def compare_and_display(self, label, other):
    """Compare this State against an OTHER State, and display differences.

    Information will be written to stdout, displaying any differences
    between the two states. LABEL will be used in the display. SELF is the
    "expected" state, and OTHER is the "actual" state.

    If any changes are detected/displayed, then SVNTreeUnequal is raised.
    """
    norm_self = self.normalize()
    norm_other = other.normalize()

    changed, unique_self, unique_other = norm_self.compare(norm_other)
    if not changed and not unique_self and not unique_other:
      return

    # Use the shortest path as a way to find the "root-most" affected node.
    def _shortest_path(path_set):
      shortest = None
      for path in path_set:
        if shortest is None or len(path) < len(shortest):
          shortest = path
      return shortest

    if changed:
      path = _shortest_path(changed)
      display_nodes(label, path, norm_self.desc[path], norm_other.desc[path])
    elif unique_self:
      path = _shortest_path(unique_self)
      default_singleton_handler('actual ' + label, path, norm_self.desc[path])
    elif unique_other:
      path = _shortest_path(unique_other)
      default_singleton_handler('expected ' + label, path,
                                norm_other.desc[path])

    raise svntest.tree.SVNTreeUnequal

  def tweak_for_entries_compare(self):
    for path, item in self.desc.copy().items():
      if item.status and path in self.desc:
        # If this is an unversioned tree-conflict, remove it.
        # These are only in their parents' THIS_DIR, they don't have entries.
        if item.status[0] in '!?' and item.treeconflict == 'C' and \
                                      item.entry_status is None:
          del self.desc[path]
        # Normal externals are not stored in the parent wc, drop the root
        # and everything in these working copies
        elif item.status == 'X ' or item.prev_status == 'X ':
          del self.desc[path]
          for p, i in self.desc.copy().items():
            if p.startswith(path + '/'):
              del self.desc[p]
        elif item.entry_kind == 'file':
          # A file has no descendants in svn_wc_entry_t
          for p, i in self.desc.copy().items():
            if p.startswith(path + '/'):
              del self.desc[p]
        else:
          # when reading the entry structures, we don't examine for text or
          # property mods, so clear those flags. we also do not examine the
          # filesystem, so we cannot detect missing or obstructed files.
          if item.status[0] in 'M!~':
            item.status = ' ' + item.status[1]
          if item.status[1] == 'M':
            item.status = item.status[0] + ' '
          # under wc-ng terms, we may report a different revision than the
          # backwards-compatible code should report. if there is a special
          # value for compatibility, then use it.
          if item.entry_rev is not None:
            item.wc_rev = item.entry_rev
            item.entry_rev = None
          # status might vary as well, e.g. when a directory is missing
          if item.entry_status is not None:
            item.status = item.entry_status
            item.entry_status = None
          if item.entry_copied is not None:
            item.copied = item.entry_copied
            item.entry_copied = None
      if item.writelocked:
        # we don't contact the repository, so our only information is what
        # is in the working copy. 'K' means we have one and it matches the
        # repos. 'O' means we don't have one but the repos says the item
        # is locked by us, elsewhere. 'T' means we have one, and the repos
        # has one, but it is now owned by somebody else. 'B' means we have
        # one, but the repos does not.
        #
        # for each case of "we have one", set the writelocked state to 'K',
        # and clear it to None for the others. this will match what is
        # generated when we examine our working copy state.
        if item.writelocked in 'TB':
          item.writelocked = 'K'
        elif item.writelocked == 'O':
          item.writelocked = None
      item.moved_from = None
      item.moved_to = None
      if path == '':
        item.switched = None
      item.treeconflict = None

  def old_tree(self):
    "Return an old-style tree (for compatibility purposes)."
    nodelist = [ ]
    for path, item in self.desc.items():
      nodelist.append(item.as_node_tuple(os.path.join(self.wc_dir, path)))

    tree = svntest.tree.build_generic_tree(nodelist)
    if 0:
      check = tree.as_state()
      if self != check:
        logger.warn(pprint.pformat(self.desc))
        logger.warn(pprint.pformat(check.desc))
        # STATE -> TREE -> STATE is lossy.
        # In many cases, TREE -> STATE -> TREE is not.
        # Even though our conversion from a TREE has lost some information, we
        # may be able to verify that our lesser-STATE produces the same TREE.
        svntest.tree.compare_trees('mismatch', tree, check.old_tree())

    return tree

  def __str__(self):
    return str(self.old_tree())

  def __eq__(self, other):
    if not isinstance(other, State):
      return False
    norm_self = self.normalize()
    norm_other = other.normalize()
    return norm_self.desc == norm_other.desc

  def __ne__(self, other):
    return not self.__eq__(other)

  @classmethod
  def from_status(cls, lines, wc_dir=None):
    """Create a State object from 'svn status' output."""

    def not_space(value):
      if value and value != ' ':
        return value
      return None

    def parse_move(path, wc_dir):
      if path.startswith('../'):
        # ../ style paths are relative from the status root
        return to_relpath(os.path.normpath(repos_join(wc_dir, path)))
      else:
        # Other paths are just relative from cwd
        return to_relpath(path)

    if not wc_dir:
      wc_dir = ''

    desc = { }
    last = None
    for line in lines:
      if line.startswith('DBG:'):
        continue

      match = _re_parse_status.search(line)
      if not match or match.group(10) == '-':

        ex_match = _re_parse_status_ex.search(line)

        if ex_match:
          if ex_match.group('moved_from'):
            path = to_relpath(ex_match.group('moved_from'))
            last.tweak(moved_from = parse_move(path, wc_dir))
          elif ex_match.group('moved_to'):
            path = to_relpath(ex_match.group('moved_to'))
            last.tweak(moved_to = parse_move(path, wc_dir))
          elif ex_match.group('swapped_with'):
            path = to_relpath(ex_match.group('swapped_with'))
            last.tweak(moved_to = parse_move(path, wc_dir))
            last.tweak(moved_from = parse_move(path, wc_dir))

          # Parse TC description?

        # ignore non-matching lines, or items that only exist on repos
        continue

      prev_status = None
      prev_treeconflict = None

      path = to_relpath(match.group('path'))
      if path == '.':
        path = ''
      if path in desc:
        prev_status = desc[path].status
        prev_treeconflict = desc[path].treeconflict

      item = StateItem(status=match.group(1),
                       locked=not_space(match.group(2)),
                       copied=not_space(match.group(3)),
                       switched=not_space(match.group(4)),
                       writelocked=not_space(match.group(5)),
                       treeconflict=not_space(match.group(6)),
                       wc_rev=not_space(match.group('wc_rev')),
                       prev_status=prev_status,
                       prev_treeconflict =prev_treeconflict
                       )
      desc[path] = item
      last = item

    return cls('', desc)

  @classmethod
  def from_skipped(cls, lines):
    """Create a State object from 'Skipped' lines."""

    desc = { }
    for line in lines:
      if line.startswith('DBG:'):
        continue

      match = _re_parse_skipped.search(line)
      if match:
        desc[to_relpath(match.group(2))] =  StateItem(
          verb=(match.group(1).strip(':')))

    return cls('', desc)

  @classmethod
  def from_summarize(cls, lines):
    """Create a State object from 'svn diff --summarize' lines."""

    desc = { }
    for line in lines:
      if line.startswith('DBG:'):
        continue

      match = _re_parse_summarize.search(line)
      if match:
        desc[to_relpath(match.group(2))] = StateItem(status=match.group(1))

    return cls('', desc)

  @classmethod
  def from_checkout(cls, lines, include_skipped=True):
    """Create a State object from 'svn checkout' lines."""

    if include_skipped:
      re_extra = _re_parse_co_skipped
    else:
      re_extra = _re_parse_co_restored

    desc = { }
    for line in lines:
      if line.startswith('DBG:'):
        continue

      match = _re_parse_checkout.search(line)
      if match:
        if match.group(3) != ' ':
          treeconflict = match.group(3)
        else:
          treeconflict = None
        path = to_relpath(match.group(4))
        prev_status = None
        prev_verb = None
        prev_treeconflict = None

        if path in desc:
          prev_status = desc[path].status
          prev_verb = desc[path].verb
          prev_treeconflict = desc[path].treeconflict

        desc[path] = StateItem(status=match.group(1),
                               treeconflict=treeconflict,
                               prev_status=prev_status,
                               prev_verb=prev_verb,
                               prev_treeconflict=prev_treeconflict)
      else:
        match = re_extra.search(line)
        if match:
          path = to_relpath(match.group(2))
          prev_status = None
          prev_verb = None
          prev_treeconflict = None

          if path in desc:
            prev_status = desc[path].status
            prev_verb = desc[path].verb
            prev_treeconflict = desc[path].treeconflict

          desc[path] = StateItem(verb=match.group(1),
                                 prev_status=prev_status,
                                 prev_verb=prev_verb,
                                 prev_treeconflict=prev_treeconflict)

    return cls('', desc)

  @classmethod
  def from_commit(cls, lines):
    """Create a State object from 'svn commit' lines."""

    desc = { }
    for line in lines:
      if line.startswith('DBG:') or line.startswith('Transmitting'):
        continue

      if line.startswith('Committing transaction'):
        continue

      match = _re_parse_commit_ext.search(line)
      if match:
        desc[to_relpath(match.group(4))] = StateItem(verb=match.group(1))
        continue

      match = _re_parse_commit.search(line)
      if match:
        desc[to_relpath(match.group(3))] = StateItem(verb=match.group(1))

    return cls('', desc)

  @classmethod
  def from_wc(cls, base, load_props=False, ignore_svn=True,
              keep_eol_style=False):
    """Create a State object from a working copy.

    Walks the tree at PATH, building a State based on the actual files
    and directories found. If LOAD_PROPS is True, then the properties
    will be loaded for all nodes (Very Expensive!). If IGNORE_SVN is
    True, then the .svn subdirectories will be excluded from the State.

    If KEEP_EOL_STYLE is set, don't let Python normalize the EOL when
    reading working copy contents as text files.  It has no effect on
    binary files.
    """
    if not base:
      # we're going to walk the base, and the OS wants "."
      base = '.'

    desc = { }
    dot_svn = svntest.main.get_admin_name()

    for dirpath, dirs, files in os.walk(base):
      parent = path_to_key(dirpath, base)
      if ignore_svn and dot_svn in dirs:
        dirs.remove(dot_svn)
      for name in dirs + files:
        node = os.path.join(dirpath, name)
        if os.path.isfile(node):
          try:
            if keep_eol_style:
              contents = open(node, 'r', newline='').read()
            else:
              contents = open(node, 'r').read()
          except:
            contents = open(node, 'rb').read()
        else:
          contents = None
        desc[repos_join(parent, name)] = StateItem(contents=contents)

    if load_props:
      paths = [os.path.join(base, to_ospath(p)) for p in desc.keys()]
      paths.append(base)
      all_props = svntest.tree.get_props(paths)
      for node, props in all_props.items():
        if node == base:
          desc['.'] = StateItem(props=props)
        else:
          if base == '.':
            # 'svn proplist' strips './' from the paths. put it back on.
            node = os.path.join('.', node)
          desc[path_to_key(node, base)].props = props

    return cls('', desc)

  @classmethod
  def from_entries(cls, base):
    """Create a State object from a working copy, via the old "entries" API.

    Walks the tree at PATH, building a State based on the information
    provided by the old entries API, as accessed via the 'entries-dump'
    program.
    """
    if not base:
      # we're going to walk the base, and the OS wants "."
      base = '.'

    if os.path.isfile(base):
      # a few tests run status on a single file. quick-and-dirty this. we
      # really should analyze the entry (similar to below) to be general.
      dirpath, basename = os.path.split(base)
      entries = svntest.main.run_entriesdump(dirpath)
      return cls('', {
          to_relpath(base): StateItem.from_entry(entries[basename]),
          })

    desc = { }
    dump_data = svntest.main.run_entriesdump_tree(base)

    if not dump_data:
      # Probably 'svn status' run on an actual only node
      # ### Improve!
      return cls('', desc)

    dirent_join = repos_join
    if len(base) == 2 and base[1:]==':' and sys.platform=='win32':
      # We have a win32 drive relative path... Auch. Fix joining
      def drive_join(a, b):
        if len(a) == 2:
          return a+b
        else:
          return repos_join(a,b)
      dirent_join = drive_join

    for parent, entries in sorted(dump_data.items()):

      parent_url = entries[''].url

      for name, entry in entries.items():
        # if the entry is marked as DELETED *and* it is something other than
        # schedule-add, then skip it. we can add a new node "over" where a
        # DELETED node lives.
        if entry.deleted and entry.schedule != 1:
          continue
        # entries that are ABSENT don't show up in status
        if entry.absent:
          continue
        # entries that are User Excluded don't show up in status
        if entry.depth == -1:
          continue
        if name and entry.kind == 2:
          # stub subdirectory. leave a "missing" StateItem in here. note
          # that we can't put the status as "! " because that gets tweaked
          # out of our expected tree.
          item = StateItem(status='  ', wc_rev='?')
          desc[dirent_join(parent, name)] = item
          continue
        item = StateItem.from_entry(entry)
        if name:
          desc[dirent_join(parent, name)] = item
          implied_url = repos_join(parent_url, svn_uri_quote(name))
        else:
          item._url = entry.url  # attach URL to directory StateItems
          desc[parent] = item

          grandpa, this_name = repos_split(parent)
          if grandpa in desc:
            implied_url = repos_join(desc[grandpa]._url,
                                     svn_uri_quote(this_name))
          else:
            implied_url = None

        if implied_url and implied_url != entry.url:
          item.switched = 'S'

        if entry.file_external:
          item.switched = 'X'

    return cls('', desc)

  @classmethod
  def from_eids(cls, lines):

    # Need to read all elements in a branch before we can construct
    # the full path to an element.
    # For the full path we use <branch-id>/<path-within-branch>.

    def eid_path(eids, eid):
      ele = eids[eid]
      if ele[0] == '-1':
        return ele[1]
      parent_path = eid_path(eids, ele[0])
      if parent_path == '':
        return ele[1]
      return parent_path + '/' + ele[1]

    def eid_full_path(eids, eid, branch_id):
      path = eid_path(eids, eid)
      if path == '':
        return branch_id
      return branch_id + '/' + path

    def add_to_desc(eids, desc, branch_id):
      for k, v in eids.items():
        desc[eid_full_path(eids, k, branch_id)] = StateItem(eid=k)

    branch_id = None
    eids = {}
    desc = {}
    for line in lines:

      match = _re_parse_eid_ele.search(line)
      if match and match.group(2) != 'none':
        eid = match.group(1)
        parent_eid = match.group(3) 
        path = match.group(4)
        if path == '.':
          path = ''
        eids[eid] = [parent_eid, path]

      match = _re_parse_eid_branch.search(line)
      if match:
        if branch_id:
          add_to_desc(eids, desc, branch_id)
          eids = {}
        branch_id = match.group(1)
        root_eid = match.group(2)

      match = _re_parse_eid_merge_history.search(line)
      if match:
        ### TODO: store the merge history
        pass

    add_to_desc(eids, desc, branch_id)

    return cls('', desc)
  

class StateItem:
  """Describes an individual item within a working copy.

  Note that the location of this item is not specified. An external
  mechanism, such as the State class, will provide location information
  for each item.
  """

  def __init__(self, contents=None, props=None,
               status=None, verb=None, wc_rev=None, entry_kind=None,
               entry_rev=None, entry_status=None, entry_copied=None,
               locked=None, copied=None, switched=None, writelocked=None,
               treeconflict=None, moved_from=None, moved_to=None,
               prev_status=None, prev_verb=None, prev_treeconflict=None,
               eid=None):
    # provide an empty prop dict if it wasn't provided
    if props is None:
      props = { }

    ### keep/make these ints one day?
    if wc_rev is not None:
      wc_rev = str(wc_rev)
    if eid is not None:
      eid = str(eid)

    # Any attribute can be None if not relevant, unless otherwise stated.

    # A string of content (if the node is a file).
    self.contents = contents
    # A dictionary mapping prop name to prop value; never None.
    self.props = props
    # A two-character string from the first two columns of 'svn status'.
    self.status = status
    self.prev_status = prev_status
    # The action word such as 'Adding' printed by commands like 'svn update'.
    self.verb = verb
    self.prev_verb = prev_verb
    # The base revision number of the node in the WC, as a string.
    self.wc_rev = wc_rev
    # If 'file' specifies that the node is a file, and as such has no svn_wc_entry_t
    # descendants
    self.entry_kind = None
    # These will be set when we expect the wc_rev/status to differ from those
    # found in the entries code.
    self.entry_rev = entry_rev
    self.entry_status = entry_status
    self.entry_copied = entry_copied
    # For the following attributes, the value is the status character of that
    # field from 'svn status', except using value None instead of status ' '.
    self.locked = locked
    self.copied = copied
    self.switched = switched
    self.writelocked = writelocked
    # Value 'C', 'A', 'D' or ' ', or None as an expected status meaning 'do not check'.
    self.treeconflict = treeconflict
    self.prev_treeconflict = prev_treeconflict
    # Relative paths to the move locations
    self.moved_from = moved_from
    self.moved_to = moved_to
    self.eid = eid

  def copy(self):
    "Make a deep copy of self."
    new = StateItem()
    vars(new).update(vars(self))
    new.props = self.props.copy()
    return new

  def tweak(self, **kw):
    for name, value in kw.items():
      # Refine the revision args (for now) to ensure they are strings.
      if value is not None and name == 'wc_rev':
        value = str(value)
      if value is not None and name == 'eid':
        value = str(value)
      setattr(self, name, value)

  def __eq__(self, other):
    if not isinstance(other, StateItem):
      return False
    v_self = dict([(k, v) for k, v in vars(self).items()
                   if not k.startswith('_') and not k.startswith('entry_')])
    v_other = dict([(k, v) for k, v in vars(other).items()
                    if not k.startswith('_') and not k.startswith('entry_')])

    if self.wc_rev == '0' and self.status == 'A ':
      v_self['wc_rev'] = '-'
    if other.wc_rev == '0' and other.status == 'A ':
      v_other['wc_rev'] = '-'
    return v_self == v_other

  def __ne__(self, other):
    return not self.__eq__(other)

  def as_node_tuple(self, path):
    atts = { }
    if self.status is not None:
      atts['status'] = self.status
    if self.prev_status is not None:
      atts['prev_status'] = self.prev_status
    if self.verb is not None:
      atts['verb'] = self.verb
    if self.prev_verb is not None:
      atts['prev_verb'] = self.prev_verb
    if self.wc_rev is not None:
      atts['wc_rev'] = self.wc_rev
    if self.locked is not None:
      atts['locked'] = self.locked
    if self.copied is not None:
      atts['copied'] = self.copied
    if self.switched is not None:
      atts['switched'] = self.switched
    if self.writelocked is not None:
      atts['writelocked'] = self.writelocked
    if self.treeconflict is not None:
      atts['treeconflict'] = self.treeconflict
    if self.prev_treeconflict is not None:
      atts['prev_treeconflict'] = self.prev_treeconflict
    if self.moved_from is not None:
      atts['moved_from'] = self.moved_from
    if self.moved_to is not None:
      atts['moved_to'] = self.moved_to
    if self.eid is not None:
      atts['eid'] = self.eid

    return (os.path.normpath(path), self.contents, self.props, atts)

  @classmethod
  def from_entry(cls, entry):
    status = '  '
    if entry.schedule == 1:  # svn_wc_schedule_add
      status = 'A '
    elif entry.schedule == 2:  # svn_wc_schedule_delete
      status = 'D '
    elif entry.schedule == 3:  # svn_wc_schedule_replace
      status = 'R '
    elif entry.conflict_old:
      ### I'm assuming we only need to check one, rather than all conflict_*
      status = 'C '

    ### is this the sufficient? guessing here w/o investigation.
    if entry.prejfile:
      status = status[0] + 'C'

    if entry.locked:
      locked = 'L'
    else:
      locked = None

    if entry.copied:
      wc_rev = '-'
      copied = '+'
    else:
      if entry.revision == -1:
        wc_rev = '?'
      else:
        wc_rev = entry.revision
      copied = None

    ### figure out switched
    switched = None

    if entry.lock_token:
      writelocked = 'K'
    else:
      writelocked = None

    return cls(status=status,
               wc_rev=wc_rev,
               locked=locked,
               copied=copied,
               switched=switched,
               writelocked=writelocked,
               )


if os.sep == '/':
  to_relpath = to_ospath = lambda path: path
else:
  def to_relpath(path):
    """Return PATH but with all native path separators changed to '/'."""
    return path.replace(os.sep, '/')
  def to_ospath(path):
    """Return PATH but with each '/' changed to the native path separator."""
    return path.replace('/', os.sep)


def path_to_key(path, base):
  """Return the relative path that represents the absolute path PATH under
  the absolute path BASE.  PATH must be a path under BASE.  The returned
  path has '/' separators."""
  if path == base:
    return ''

  if base.endswith(os.sep) or base.endswith('/') or base.endswith(':'):
    # Special path format on Windows:
    #  'C:/' Is a valid root which includes its separator ('C:/file')
    #  'C:'  is a valid root which isn't followed by a separator ('C:file')
    #
    # In this case, we don't need a separator between the base and the path.
    pass
  else:
    # Account for a separator between the base and the relpath we're creating
    base += os.sep

  assert path.startswith(base), "'%s' is not a prefix of '%s'" % (base, path)
  return to_relpath(path[len(base):])


def repos_split(repos_relpath):
  """Split a repos path into its directory and basename parts."""
  idx = repos_relpath.rfind('/')
  if idx == -1:
    return '', repos_relpath
  return repos_relpath[:idx], repos_relpath[idx+1:]


def repos_join(base, path):
  """Join two repos paths. This generally works for URLs too."""
  if base == '':
    return path
  elif path == '':
    return base
  elif base[len(base)-1:] == '/':
    return base + path
  else:
    return base + '/' + path


def svn_uri_quote(url):
  # svn defines a different set of "safe" characters than Python does, so
  # we need to avoid escaping them. see subr/path.c:uri_char_validity[]
  return urllib_quote(url, "!$&'()*+,-./:=@_~")


# ------------

def python_sqlite_can_read_wc():
  """Check if the Python builtin is capable enough to peek into wc.db"""
  # Currently enough (1.7-1.9)
  return svntest.sqlite3.sqlite_version_info >= (3, 6, 18)

def open_wc_db(local_path):
  """Open the SQLite DB for the WC path LOCAL_PATH.
     Return (DB object, WC root path, WC relpath of LOCAL_PATH)."""
  dot_svn = svntest.main.get_admin_name()
  root_path = local_path
  relpath = ''

  while True:
    db_path = os.path.join(root_path, dot_svn, 'wc.db')
    try:
      db = svntest.sqlite3.connect(db_path)
      break
    except: pass
    head, tail = os.path.split(root_path)
    if head == root_path:
      raise svntest.Failure("No DB for " + local_path)
    root_path = head
    relpath = os.path.join(tail, relpath).replace(os.path.sep, '/').rstrip('/')

  return db, root_path, relpath

# ------------

def text_base_path(file_path):
  """Return the path to the text-base file for the versioned file
     FILE_PATH."""

  info = svntest.actions.run_and_parse_info(file_path)[0]

  checksum = info['Checksum']
  db, root_path, relpath = open_wc_db(file_path)

  # Calculate single DB location
  dot_svn = svntest.main.get_admin_name()
  fn = os.path.join(root_path, dot_svn, 'pristine', checksum[0:2], checksum)

  # For SVN_WC__VERSION < 29
  if os.path.isfile(fn):
    return fn

  # For SVN_WC__VERSION >= 29
  if os.path.isfile(fn + ".svn-base"):
    return fn + ".svn-base"

  raise svntest.Failure("No pristine text for " + relpath)

def sqlite_stmt(wc_root_path, stmt):
  """Execute STMT on the SQLite wc.db in WC_ROOT_PATH and return the
     results."""

  db = open_wc_db(wc_root_path)[0]
  c = db.cursor()
  c.execute(stmt)
  return c.fetchall()

def sqlite_exec(wc_root_path, stmt):
  """Execute STMT on the SQLite wc.db in WC_ROOT_PATH and return the
     results."""

  db = open_wc_db(wc_root_path)[0]
  c = db.cursor()
  c.execute(stmt)
  db.commit()


# ------------
### probably toss these at some point. or major rework. or something.
### just bootstrapping some changes for now.
#

def item_to_node(path, item):
  tree = svntest.tree.build_generic_tree([item.as_node_tuple(path)])
  while tree.children:
    assert len(tree.children) == 1
    tree = tree.children[0]
  return tree

### yanked from tree.compare_trees()
def display_nodes(label, path, expected, actual):
  'Display two nodes, expected and actual.'
  expected = item_to_node(path, expected)
  actual = item_to_node(path, actual)

  o = StringIO()
  o.write("=============================================================\n")
  o.write("Expected '%s' and actual '%s' in %s tree are different!\n"
                % (expected.name, actual.name, label))
  o.write("=============================================================\n")
  o.write("EXPECTED NODE TO BE:\n")
  o.write("=============================================================\n")
  expected.pprint(o)
  o.write("=============================================================\n")
  o.write("ACTUAL NODE FOUND:\n")
  o.write("=============================================================\n")
  actual.pprint(o)

  logger.warn(o.getvalue())
  o.close()

### yanked from tree.py
def default_singleton_handler(description, path, item):
  node = item_to_node(path, item)
  logger.warn("Couldn't find node '%s' in %s tree" % (node.name, description))
  o = StringIO()
  node.pprint(o)
  logger.warn(o.getvalue())
  o.close()
  raise svntest.tree.SVNTreeUnequal
