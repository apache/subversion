#
#  wc.py: functions for interacting with a Subversion working copy
#
#  Subversion is a tool for revision control. 
#  See http://subversion.tigris.org for more information.
#    
# ====================================================================
# Copyright (c) 2000-2001 CollabNet.  All rights reserved.
#
# This software is licensed as described in the file COPYING, which
# you should have received as part of this distribution.  The terms
# are also available at http://subversion.tigris.org/license-1.html.
# If newer versions of this license are posted there, you may use a
# newer version instead, at your option.
#
######################################################################

import os
import types

import svntest.tree


class State:
  """Describes an existing or expected state of a working copy.

  The primary metaphor here is a dictionary of paths mapping to instances
  of StateItem, which describe each item in a working copy.

  Note: the paths should be *relative* to the root of the working copy.
  """

  def __init__(self, wc_dir, desc):
    "Create a State using the specified description."
    assert isinstance(desc, types.DictionaryType)

    self.wc_dir = wc_dir
    self.desc = desc      # dictionary: path -> StateItem

  def add(self, more_desc):
    "Add more state items into the State."
    assert isinstance(more_desc, types.DictionaryType)

    self.desc.update(more_desc)

  def remove(self, *paths):
    "Remove a path from the state (the path must exist)."
    for path in paths:
      del self.desc[path]

  def copy(self):
    "Make a deep copy of self."
    desc = { }
    for path, item in self.desc.items():
      desc[path] = item.copy()
    return State(self.wc_dir, desc)

  def tweak(self, *args, **kw):
    """Tweak the items' values, optional restricting based on a filter.

    The general form of this method is .tweak(paths..., key=value). If
    one or paths are provided, then those items' values are modified.
    If no paths are given, then all items are modified.
    """
    if args:
      for path in args:
        apply(self.desc[path].tweak, (), kw)
    else:
      for item in self.desc.values():
        apply(item.tweak, (), kw)

  def tweak_some(self, filter, **kw):
    "Tweak the items for which the filter returns true."
    for path, item in self.desc.items():
      if filter(path, item):
        apply(item.tweak, (), kw)

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
        open(fullpath, 'w').write(item.contents)

  def old_tree(self):
    "Return an old-style tree (for compatibility purposes)."
    nodelist = [ ]
    for path, item in self.desc.items():
      atts = { }
      if item.status is not None:
        atts['status'] = item.status
      if item.verb is not None:
        atts['verb'] = item.verb
      if item.wc_rev is not None:
        atts['wc_rev'] = item.wc_rev
      if item.repos_rev is not None:
        atts['repos_rev'] = item.repos_rev
      if item.locked is not None:
        atts['locked'] = item.locked
      if item.copied is not None:
        atts['copied'] = item.copied
      nodelist.append((os.path.normpath(os.path.join(self.wc_dir, path)),
                       item.contents,
                       item.props,
                       atts))

    return svntest.tree.build_generic_tree(nodelist)


class StateItem:
  """Describes an individual item within a working copy.

  Note that the location of this item is not specified. An external
  mechanism, such as the State class, will provide location information
  for each item.
  """

  def __init__(self, contents=None, props=None,
               status=None, verb=None, wc_rev=None, repos_rev=None,
               locked=None, copied=None):
    # provide an empty prop dict if it wasn't provided
    if props is None:
      props = { }

    ### keep/make these ints one day?
    if wc_rev is not None:
      wc_rev = str(wc_rev)
    if repos_rev is not None:
      repos_rev = str(repos_rev)

    self.contents = contents
    self.props = props
    self.status = status
    self.verb = verb
    self.wc_rev = wc_rev
    self.repos_rev = repos_rev
    self.locked = locked
    self.copied = copied

  def copy(self):
    "Make a deep copy of self."
    new = StateItem()
    vars(new).update(vars(self))
    new.props = self.props.copy()
    return new

  def tweak(self, **kw):
    for name, value in kw.items():
      ### refine the revision args (for now) to ensure they are strings
      if name == 'wc_rev' or name == 'repos_rev':
        value = str(value)
      setattr(self, name, value)
