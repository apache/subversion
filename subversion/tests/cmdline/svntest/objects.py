#!/usr/bin/env python
#
#  objects.py: Objects that keep track of state during a test
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

# General modules
import shutil, sys, re, os, subprocess

# Our testing module
import svntest
from svntest import actions, main, tree, verify, wc


######################################################################
# Helpers

def local_path(path):
  """Convert a path from internal style ('/' separators) to the local style."""
  if path == '':
    path = '.'
  return os.sep.join(path.split('/'))


def db_dump(db_dump_name, repo_path, table):
  """Yield a human-readable representation of the rows of the BDB table
  TABLE in the repo at REPO_PATH.  Yield one line of text at a time.
  Calls the external program "db_dump" which is supplied with BDB."""
  table_path = repo_path + "/db/" + table
  process = subprocess.Popen([db_dump_name, "-p", table_path],
                             stdout=subprocess.PIPE, universal_newlines=True)
  retcode = process.wait()
  assert retcode == 0

  # Strip out the header and footer; copy the rest into FILE.
  copying = False
  for line in process.stdout.readlines():
    if line == "HEADER=END\n":
      copying = True;
    elif line == "DATA=END\n":
      break
    elif copying:
      yield line

def crude_bdb_parse(line):
  """Replace BDB ([klen] kval) syntax with (kval) syntax. This is a very
  crude parser, only just good enough to make a more human-friendly output
  in common cases. The result is not intended to be unambiguous or machine-
  parsable."""
  lparts = line.split('(')
  new_lparts = []
  for lpart in lparts:
    rparts = lpart.split(')')
    new_rparts = []
    for rpart in rparts:
      words = rpart.split(' ')

      # Set new_words to all the words that we want to keep
      new_words = []
      last_len = None
      last_word = None
      for word in words:
        if last_len is None:
          try:
            last_len = int(word)
            last_word = word
          except ValueError:
            if last_word is not None:
              new_words += [last_word]
            new_words += [word]
            last_word = None
            last_len = None
        else:  # print a word that was previously prefixed by its len
          if not len(word) == last_len + word.count('\\') * 2:
            print "## parsed wrongly: %d %d '%s'" % (last_len, len(word), word)
          if word == "":
            new_words += ["''"]
          else:
            new_words += [word]
          last_word = None
          last_len = None

      if last_word is not None:
        new_words += [last_word]

      new_rparts += [' '.join(new_words)]
    new_lparts += [')'.join(new_rparts)]
  new_line = '('.join(new_lparts)
  return new_line

def dump_bdb(db_dump_name, repo_path, dump_dir):
  """Dump all the known BDB tables in the repository at REPO_PATH into a
  single text file in DUMP_DIR.  Omit any "next-key" records."""
  dump_file = dump_dir + "/all.bdb"
  file = open(dump_file, 'w')
  for table in ['revisions', 'transactions', 'changes', 'copies', 'nodes',
                'node-origins', 'representations', 'checksum-reps', 'strings',
                'locks', 'lock-tokens', 'miscellaneous', 'uuids']:
    file.write(table + ":\n")
    for line in db_dump(db_dump_name, repo_path, table):
      file.write(crude_bdb_parse(line))
    file.write("\n")
  file.close()

  # Dump the svn view of the repository
  #svnadmin dump    repo_path > dump_dir + "/dump.svn"
  #svnadmin lslocks repo_path > dump_dir + "/locks.svn"
  #svnadmin lstxns  repo_path > dump_dir + "/txns.svn"

def locate_db_dump():
  """Locate a db_dump executable"""
  # Assume that using the newest version is OK.
  for db_dump_name in ['db4.8_dump', 'db4.7_dump', 'db4.6_dump',
                       'db4.5_dump', 'db4.4_dump', 'db_dump']:
    try:
      if subprocess.Popen([db_dump_name, "-V"]).wait() == 0:
        return db_dump_name
    except OSError, e:
      pass
  return 'none'

######################################################################
# Class SvnRepository

class SvnRepository:
  """An object of class SvnRepository represents a Subversion repository,
     providing both a client-side view and a server-side view."""

  def __init__(self, repo_url, repo_dir):
    self.repo_url = repo_url
    self.repo_absdir = os.path.abspath(repo_dir)
    self.db_dump_name = locate_db_dump()

  def dump(self, output_dir):
    """Dump the repository into the directory OUTPUT_DIR"""
    ldir = local_path(output_dir)
    os.mkdir(ldir)
    print "## SvnRepository::dump(rep_dir=" + self.repo_absdir + ")"

    """Run a BDB dump on the repository"""
    if self.db_dump_name != 'none':
      dump_bdb(self.db_dump_name, self.repo_absdir, ldir)

    """Run 'svnadmin dump' on the repository."""
    exit_code, stdout, stderr = \
      actions.run_and_verify_svnadmin(None, None, None,
                                      'dump', self.repo_absdir)
    ldumpfile = local_path(output_dir + "/svnadmin.dump")
    main.file_write(ldumpfile, ''.join(stderr))
    main.file_append(ldumpfile, ''.join(stdout))


  def obliterate_node_rev(self, path, rev):
    """Obliterate the single node-rev PATH in revision REV."""
    actions.run_and_verify_svn(None, None, [],
                               'obliterate',
                               self.repo_url + '/' + path + '@' + str(rev))


######################################################################
# Class SvnWC

class SvnWC:
  """An object of class SvnWC represents a WC, and provides methods for
     operating on it. It keeps track of the state of the WC and of the
     repository, so that the expected results of common operations are
     automatically known.

     Path arguments to class methods paths are relative to the WC dir and
     in Subversion canonical form ('/' separators).
     """

  def __init__(self, wc_dir):
    """Initialize the object to use the existing WC at path WC_DIR.
    """
    self.wc_absdir = os.path.abspath(wc_dir)
    # 'state' is, at all times, the 'wc.State' representation of the state
    # of the WC, with paths relative to 'wc_absdir'.
    #self.state = wc.State('', {})
    initial_wc_tree = tree.build_tree_from_wc(self.wc_absdir, load_props=True)
    self.state = initial_wc_tree.as_state()
    self.state.add({
      '': wc.StateItem()
    })
    # This WC's idea of the repository's head revision.
    # ### Shouldn't be in this class: should ask the repository instead.
    self.head_rev = 0

    print "## new SvnWC:"
    print self

  def __str__(self):
    return "SvnWC(head_rev=" + str(self.head_rev) + ", state={" + \
      str(self.state.desc) + \
      "})"

  def svn_mkdir(self, rpath):
    lpath = local_path(rpath)
    actions.run_and_verify_svn(None, None, [], 'mkdir', lpath)

    self.state.add({
      rpath : wc.StateItem(status='A ')
    })

#  def propset(self, pname, pvalue, *rpaths):
#    "Set property 'pname' to value 'pvalue' on each path in 'rpaths'"
#    local_paths = tuple([local_path(rpath) for rpath in rpaths])
#    actions.run_and_verify_svn(None, None, [], 'propset', pname, pvalue,
#                               *local_paths)

  def svn_set_props(self, rpath, props):
    """Change the properties of PATH to be the dictionary {name -> value} PROPS.
    """
    lpath = local_path(rpath)
    #for prop in path's existing props:
    #  actions.run_and_verify_svn(None, None, [], 'propdel',
    #                             prop, lpath)
    for prop in props:
      actions.run_and_verify_svn(None, None, [], 'propset',
                                 prop, props[prop], lpath)
    self.state.tweak(rpath, props=props)

  def svn_file_create_add(self, rpath, content=None, props=None):
    "Make and add a file with some default content, and keyword expansion."
    lpath = local_path(rpath)
    ldirname, filename = os.path.split(lpath)
    if content is None:
      # Default content
      content = "This is the file '" + filename + "'.\n" + \
                "Last changed in '$Revision$'.\n"
    main.file_write(lpath, content)
    actions.run_and_verify_svn(None, None, [], 'add', lpath)

    self.state.add({
      rpath : wc.StateItem(status='A ')
    })
    if props is None:
      # Default props
      props = {
        'svn:keywords': 'Revision'
      }
    self.svn_set_props(rpath, props)

  def file_modify(self, rpath, content=None, props=None):
    "Make text and property mods to a WC file."
    lpath = local_path(rpath)
    if content is not None:
      #main.file_append(lpath, "An extra line.\n")
      #actions.run_and_verify_svn(None, None, [], 'propset',
      #                           'newprop', 'v', lpath)
      main.file_write(lpath, content)
      self.state.tweak(rpath, content=content)
    if props is not None:
      self.set_props(rpath, props)
      self.state.tweak(rpath, props=props)

  def svn_move(self, rpath1, rpath2, parents=False):
    """Move/rename the existing WC item RPATH1 to become RPATH2.
    RPATH2 must not already exist. If PARENTS is true, any missing parents
    of RPATH2 will be created."""
    lpath1 = local_path(rpath1)
    lpath2 = local_path(rpath2)
    args = [lpath1, lpath2]
    if parents:
      args += ['--parents']
    actions.run_and_verify_svn(None, None, [], 'copy', *args)
    self.state.add({
      rpath2: self.state.desc[rpath1]
    })
    self.state.remove(rpath1)

  def svn_copy(self, rpath1, rpath2, parents=False, rev=None):
    """Copy the existing WC item RPATH1 to become RPATH2.
    RPATH2 must not already exist. If PARENTS is true, any missing parents
    of RPATH2 will be created. If REV is not None, copy revision REV of
    the node identified by WC item RPATH1."""
    lpath1 = local_path(rpath1)
    lpath2 = local_path(rpath2)
    args = [lpath1, lpath2]
    if rev is not None:
      args += ['-r', rev]
    if parents:
      args += ['--parents']
    actions.run_and_verify_svn(None, None, [], 'copy', *args)
    self.state.add({
      rpath2: self.state.desc[rpath1]
    })

  def svn_delete(self, rpath):
    "Delete a WC path locally."
    lpath = local_path(rpath)
    actions.run_and_verify_svn(None, None, [], 'delete', lpath)

  def svn_commit(self, rpath='', log=''):
    "Commit a WC path (recursively). Return the new revision number."
    lpath = local_path(rpath)
    actions.run_and_verify_svn(None, verify.AnyOutput, [],
                               'commit', '-m', log, lpath)
    actions.run_and_verify_update(lpath, None, None, None)
    self.head_rev += 1
    print "## head-rev == " + str(self.head_rev)
    return self.head_rev

#  def svn_merge(self, rev_spec, source, target, exp_out=None):
#    """Merge a single change from path 'source' to path 'target'.
#    SRC_CHANGE_NUM is either a number (to cherry-pick that specific change)
#    or a command-line option revision range string such as '-r10:20'."""
#    lsource = local_path(source)
#    ltarget = local_path(target)
#    if isinstance(rev_spec, int):
#      rev_spec = '-c' + str(rev_spec)
#    if exp_out is None:
#      target_re = re.escape(target)
#      exp_1 = "--- Merging r.* into '" + target_re + ".*':"
#      exp_2 = "(A |D |[UG] | [UG]|[UG][UG])   " + target_re + ".*"
#      exp_out = verify.RegexOutput(exp_1 + "|" + exp_2)
#    actions.run_and_verify_svn(None, exp_out, [],
#                               'merge', rev_spec, lsource, ltarget)

