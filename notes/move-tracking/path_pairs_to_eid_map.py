#!/usr/bin/env python

# Usage: path_pairs_to_eid_map.py [INITIAL-PATH FINAL-PATH ...]
#
# Convert a list of (initial_path, final_path) pairs to a pair of element
# mappings:
#   initial_map = {eid: (parent_eid, name), ...}
#   final_map = {eid: (parent_eid, name), ...}
#
# Assign an EID for each input path, and for each parent directory of each
# input path. For example, with input [(A/B, X/Y)], assign EIDs for three
# elements: (A -> unspecified), (unspecified -> X), (A/B -> X/Y)].
#
# When assigning an EID to a parent directory whose mapping is not specified
# by the inputs, let both the initial and final sides of the mapping have
# the same name and the same parent-eid. Continuing the previous example,
# assume (A -> A) and (X -> X). Another example: for input [(A -> X),
# (A/B/C -> X/B/D)], assume (A/B -> X/B).
#
# When the (parent-eid, name) of an implied path on one side already exists
# on the other side, don't add a duplicate location; instead, assume it
# doesn't exist on that other side. Example: for input [(A A2) (A/B A/B2)],
# don't output [(A -> A2), (A -> A), ...],
# but rather [(A -> A2), (None -> A), ...].

import sys
import posixpath
import collections

class ArgumentsError(Exception):
  pass

# input: a list of pairs of paths
input_example_1 = [
  ("A/D/H", "A"),
  (None, "A/D"),  # this one's optional; it would be deduced if not specified
  ("A", "A/D/H"),
]
input_example_2 = [
  ("A/B/C/D", "A"),
  ("A", "A/B/C/D"),
]
input_path_pairs = input_example_1

# Read input from pairs of command-line arguments, if given.
if len(sys.argv) > 1:
  n_args = len(sys.argv) - 1
  if n_args % 2:
    raise ArgumentsError("Need an even number (not %d) of paths, to be used in pairs" %
                         n_args)
  argv = sys.argv[1:]
  argv = [None if (a == '' or a == 'None' or a == 'nil') else a
          for a in argv]
  input_path_pairs = zip(argv[::2], argv[1::2])

print("Input:")
for e in input_path_pairs:
  print("  " + str(e))

next_eid = len(input_path_pairs) + 1

def get_next_eid():
  global next_eid
  new_eid = next_eid
  next_eid += 1
  return new_eid

def split_path(path):
  """Convert PATH to (parent-path, name), unless it is None.
  """
  return posixpath.split(path) if path is not None else None

class DictWithUniqueValues(dict):
  # Overriding __setitem__ won't catch all possible ways to set a value
  # (e.g. __init__, update, setdefault) but is good enough for us here.
  def __setitem__(self, k, v):
    """Ensure no duplicate value already exists."""
    assert v not in self.values(), (k, v)
    dict.__setitem__(self, k, v)

class MappingEidToPpathName(DictWithUniqueValues):
  """A mapping from EID to (parent_path, name).
  """
  def eid_from_relpath(self, relpath):
    """Return the EID for RELPATH, or -1 if the EID for RELPATH is not known.
    """
    if relpath == '':
      return 0
    parent_path, name = posixpath.split(relpath)
    for eid, loc in self.items():
      if loc == (parent_path, name):
        return eid
    return -1

class MappingEidToPeidName(DictWithUniqueValues):
  """A mapping from EID to (parent_eid, name).
  """
  def eid_from_relpath(self, relpath):
    """Return the EID for RELPATH, or -1 if the EID for RELPATH is not known.
    """
    if relpath == '':
      return 0
    parent_path, name = posixpath.split(relpath)
    for eid, loc in self.items():
      if loc[1] == name and loc[0] == self.eid_from_relpath(parent_path):
        return eid
    return -1

  def eid_from_loc(self, loc):
    """Return the EID for LOC, or -1 if the EID for LOC is not known.
       LOC is (parent_eid, name).
    """
    if loc is None:
      return 0
    for eid, this_loc in self.items():
      if loc == this_loc:
        return eid
    return -1

  def relpath_from_eid(self, eid):
    """Return the relpath of element EID in a mapping from EID to
       (parent_eid, name).
    """
    if eid == 0:
      return ''
    element = self.get(eid)
    if element is None:
      return None
    parent_eid, name = element
    parent_path = self.relpath_from_eid(parent_eid)
    if parent_path is None:
      return None
    return posixpath.join(parent_path, name)

class InitialFinalConverter:
  def __init__(self, path_pairs=[]):
    assert all([path is None or type(path) is str
                for pair in path_pairs for path in pair])
    # Convert to a list of pairs of split-path:
    # [[initial_split_path, final_split_path], ...],
    # where split-path is (parent-path, name).
    split_path_pairs = [[split_path(path) for path in path_pair]
                        for path_pair in path_pairs]
    # Convert to (initial, final) lists of split paths:
    # ([split-path, ...], [split-path, ...])
    two_lists = zip(*split_path_pairs)
    # Convert to (initial, final) eid-to-split-path mappings, assigning
    # EIDs starting at EID 1:
    # ({ eid:split-path, ... }, { eid:split-path, ... })
    self.path_maps = (MappingEidToPpathName(enumerate(two_lists[0], 1)),
                      MappingEidToPpathName(enumerate(two_lists[1], 1)))
    self.peid_maps = (MappingEidToPeidName(), MappingEidToPeidName())

  def path_loc_pairs(self):
    return { eid: [self.path_maps[0].get(eid), self.path_maps[1].get(eid)]
             for eid in set(self.path_maps[0]) | set(self.path_maps[1]) }

  def peid_loc_pairs(self):
    return { eid: [self.peid_maps[0].get(eid), self.peid_maps[1].get(eid)]
             for eid in set(self.peid_maps[0]) | set(self.peid_maps[1]) }

  def path_locs_for_side(self, side):
    return self.path_maps[side]

  def peid_locs_for_side(self, side):
    return self.peid_maps[side]

  def has_peid_loc(self, side, loc):
    assert loc and type(loc[0]) is int
    return self.peid_locs_for_side(side).eid_from_loc(loc) >= 0

  def set_peid_loc(self, side, eid, loc):
    """Set the mapping for SIDE:EID to LOC. (If no mapping for EID already
       exists, implicitly set the other side to None.)
       LOC is (parent-eid, name).
    """
    assert type(loc[0]) is int
    self.peid_maps[side][eid] = loc

  def find_eid_from_relpath(self, side, relpath):
    """Return the EID for SIDE:RELPATH, or -1 if not found.
    """
    eid = self.path_locs_for_side(side).eid_from_relpath(relpath)
    if eid < 0:
      eid = self.peid_locs_for_side(side).eid_from_relpath(relpath)
    if eid < 0:
      # Look up using a combined search in both (parent-eid, name) and
      # (parent-relpath, name) maps, if necessary.
      ### Not sure if this would ever be necessary.
      pass
    return eid

# Assign an EID to each input pair of split-paths, starting at EID 1.
converter = InitialFinalConverter(input_path_pairs)

print("Input, as (initial, final) mappings by (parent-path, name):")
for eid, locs in converter.path_loc_pairs().items():
  print("  " + str(eid) + ": " + str(locs))

def add_eid_mapping_and_make_parents(mapping, side, eid, parent_path, name):
  """Add a (parent_eid, name) entry for SIDE:EID, and for each of its parent
     paths that lacks an EID, up to a path that has an EID.
     Add this same mapping to the other side as well, but without caring
     whether the parent element exists on the other side. ### Is this right?
  """
  parent_eid = mapping.find_eid_from_relpath(side, parent_path)
  if parent_eid < 0:
    # no eid assigned yet for this path: assign a new eid for it
    parent_eid = add_new(mapping, side, parent_path)
  loc = (parent_eid, name)
  mapping.set_peid_loc(side, eid, loc)
  return loc

def add_new(mapping, side, path):
  """Add a new EID and (parent_eid, name) entry for PATH, and for each
     of its parents that lacks an EID.

     Add this same mapping to the other side as well, but without caring
     whether the parent element exists on the other side.
       ### Why is this right?
  """
  eid = get_next_eid()
  parent_path, name = posixpath.split(path)
  loc = add_eid_mapping_and_make_parents(mapping, side, eid, parent_path, name)
  if not mapping.has_peid_loc(1 - side, loc):
    mapping.set_peid_loc(1 - side, eid, loc)
  return eid

def write_parent_eid(mapping, side, eid):
  """Write a (parent_eid, name) mapping corresponding to the existing
     (parent-path, name) mapping for SIDE:EID.

     For each of its parent paths in SIDE that lacks an EID, up to a path
     that has an EID, allocate an EID and write a (parent-eid, name) mapping
     in BOTH sides.
  """
  path_loc = mapping.path_locs_for_side(side)[eid]
  parent_path, name = path_loc
  new_loc = add_eid_mapping_and_make_parents(mapping, side, eid, parent_path, name)
  print("# converting e%d: %s -> %s" % (eid, str(path_loc), str(new_loc)))

# Convert parent-path mappings to parent-EID mappings.
# Add new elements as we go for any previously unlisted parent paths.
for side in [0, 1]:
  for eid in converter.path_locs_for_side(side):
    # We don't write any of these entries twice, so it shouldn't already be
    # here:
    assert eid not in converter.peid_locs_for_side(side)
    write_parent_eid(converter, side, eid)

print("Output, as (initial, final) mappings by (parent-eid, name):")
for eid, locs in converter.peid_loc_pairs().items():
  print("  " + str(eid) + ": " + str(locs))

print("Output, as (initial, final) mappings by paths:")
for eid in converter.peid_loc_pairs():
  relpath0 = converter.peid_locs_for_side(0).relpath_from_eid(eid)
  relpath1 = converter.peid_locs_for_side(1).relpath_from_eid(eid)
  print("%3d %-12s %-12s" % (eid, relpath0, relpath1))

