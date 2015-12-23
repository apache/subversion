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

def split_path(path):
  """Convert PATH to (parent-path, name), unless it is None.
  """
  return posixpath.split(path) if path is not None else None

def split_paths(path_pairs):
  """Convert each path in PATH_PAIRS to (parent-path, name), unless it is None.
  """
  return [[split_path(path) for path in path_pair]
          for path_pair in path_pairs]

# Convert to (initial, final) mappings by "split path",
# where split-path is (parent-path, name).
input_pairs = split_paths(input_path_pairs)

# Assign an EID to each input pair of split-paths, starting at EID 1.
out_map = dict(enumerate(input_pairs, 1))

# ### Experimental section...
# Convert to (initial, final) lists of split paths:
# ([split-path, ...], [split-path, ...])
input_as_two_lists = zip(*input_pairs)
# Convert to (initial, final) eid-to-split-path mappings, starting at EID 1:
# ({ eid:split-path, ... }, { eid:split-path, ... })
split_path_maps = (dict(enumerate(input_as_two_lists[0], 1)),
                   dict(enumerate(input_as_two_lists[1], 1)))

next_eid = len(input_pairs) + 1

def get_next_eid():
  global next_eid
  new_eid = next_eid
  next_eid += 1
  return new_eid

print("Input, as (intial, final) mappings by (parent-path, name):")
for e in out_map.items():
  print("  " + str(e))

def map_set(out_map, side, eid, loc):
  """Set the mapping for EID to LOC. If no mapping for EID already exists,
     set the other side to None.
     LOC is either (parent-path, name) or (parent-eid, name).
   """
  #print("map_set(%d, %s)" % (eid, str(loc)))
  entry = out_map.get(eid, [None, None])
  entry[side] = loc
  out_map[eid] = entry

def find_eid_from_path(out_map, side, path):
  """Return the EID for PATH, or -1 if the EID for PATH is not known.
  """
  if path == '':
    return 0
  for eid, entry in out_map.items():
    loc = entry[side]
    if loc:
      parent_path, name = posixpath.split(path)
      if loc[1] == name:
        if (loc[0] == parent_path or
            loc[0] == find_eid_from_path(out_map, side, parent_path)):
          return eid

  return -1

def find_eid_from_loc(out_map, side, loc):
  """Return the EID for LOC, or -1 if the EID for LOC is not known.
  """
  if loc is None:
    return 0
  for eid, entry in out_map.items():
    if loc == entry[side]:
      return eid
  return -1

def add_new(out_map, side, path):
  """Add a new EID and (parent_eid, name) entry for PATH, and for each
     of its parents that lacks one.
     Add this same mapping to the other side as well, but without caring
     whether the parent element exists on the other side. ### Is this right?
  """
  new_eid = get_next_eid()
  parent_path, name = posixpath.split(path)
  parent_eid = find_or_add(out_map, side, parent_path)
  loc = (parent_eid, name)
  map_set(out_map, side, new_eid, loc)
  #if loc not in out_map[1 - side]:
  if find_eid_from_loc(out_map, side, loc) < 0:
    map_set(out_map, 1 - side, new_eid, loc)
  return new_eid

def find_or_add(out_map, side, path):
  """Find the EID for PATH, creating a new EID and (parent-eid, name) entry
     for each of PATH and its parents that is not already present.
  """
  eid = find_eid_from_path(out_map, side, path)
  if eid < 0:
    # no eid assigned yet for this path: assign a new eid for it
    eid = add_new(out_map, side, path)
  return eid

def write_parent_eid(out_map, side, eid):
  """Ensure the SIDE mapping for EID (if any) uses a parent_eid rather than
     a path. (Also ensure that its parent has some sort of mapping.)
  """
  entry = out_map[eid]
  loc = entry[side]
  if loc:
    parent, name = loc
    if type(parent) is int:
      print("# converting e%d: %s already has a parent-eid" % (eid, str(loc)))
      return
    parent_eid = find_or_add(out_map, side, parent)
    new_loc = (parent_eid, name)
    print("# converting e%d: %s -> %s" % (eid, str(loc), str(new_loc)))
    map_set(out_map, side, eid, new_loc)

# Change parent paths to parent eids (in place). For each location in OUT_MAP,
# convert its (parent-path, name) to (parent-eid, name), adding new elements as
# we go for any previously unlisted parent paths.
for side in [0, 1]:
  for eid, entry in out_map.items():
    write_parent_eid(out_map, side, eid)

print("Output, as (initial, final) mappings by (parent-eid, name):")
for e in out_map.items():
  print("  " + str(e))

def map_get_path(eid_map, side, eid):
  if eid == 0:
    return ''
  element = eid_map[eid][side]
  if element is None:
    return None
  parent_eid, name = element
  parent_path = map_get_path(eid_map, side, parent_eid)
  if parent_path is None:
    return None
  return posixpath.join(parent_path, name)

print("Output, as (initial, final) mappings by paths:")
for eid, entry in out_map.items():
  relpath0 = map_get_path(out_map, 0, eid)
  relpath1 = map_get_path(out_map, 1, eid)
  print "%3d %-12s %-12s" % (eid, relpath0, relpath1)

