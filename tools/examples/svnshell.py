#!/usr/bin/env python2
#
# svnshell.py : a Python-based shell interface for cruising 'round in
#               the filesystem.
#
######################################################################
#
# Copyright (c) 2000-2003 CollabNet.  All rights reserved.
#
# This software is licensed as described in the file COPYING, which
# you should have received as part of this distribution.  The terms
# are also available at http://subversion.tigris.org/license-1.html.
# If newer versions of this license are posted there, you may use a
# newer version instead, at your option.
#
######################################################################
#

import sys
import string
import time
import re
from random import randint
from svn import fs, util, repos


class SVNShell:
  def __init__(self, pool, path):
    """initialize an SVNShell object"""
    if path[-1] == '/':
      path = path[:-1]
    self.pool = pool
    self.taskpool = util.svn_pool_create(pool)
    self.fs_ptr = repos.svn_repos_fs(repos.svn_repos_open(path, pool))
    self.is_rev = 1
    self.rev = fs.youngest_rev(self.fs_ptr, pool)
    self.txn = None
    self.root = fs.revision_root(self.fs_ptr, self.rev, pool)
    self.path = "/"
    self._do_prompt()

  def cmd_help(self, *args):
    """print shell help"""
    print "Available commands:"
    print "  cat FILE     : dump the contents of FILE"
    print "  cd DIR       : change the current working directory to DIR"
    print "  exit         : exit the shell"
    print "  ls [PATH]    : list the contents of the current directory"
    print "  lstxns       : list the transactions available for browsing"
    print "  pcat [PATH]  : list the properties of PATH"
    print "  setrev REV   : set the current revision to browse"
    print "  settxn TXN   : set the current transaction to browse"
    print "  youngest     : list the youngest browsable revision number"
    
  def cmd_cat(self, *args):
    """dump the contents of a file"""
    args = args[0]
    if not len(args):
      print "You must supply a file path."
      return
    catpath = self._parse_path(args[0])
    kind = fs.check_path(self.root, catpath, self.taskpool)
    if kind == util.svn_node_none:
      print "Path '%s' does not exist." % catpath
      return
    if kind == util.svn_node_dir:
      print "Path '%s' is not a file." % catpath
      return
    ### be nice to get some paging in here.  also, not reading the
    ### whole contents of the file at once.  but whatever.
    filelen = fs.file_length(self.root, catpath, self.taskpool)
    stream = fs.file_contents(self.root, catpath, self.taskpool)
    print util.svn_stream_read(stream, filelen)
    
  def cmd_cd(self, *args):
    """change directory"""
    args = args[0]
    if len(args) < 1:
      return
    newpath = self._parse_path(args[0])
    
    # make sure that path actually exists in the filesystem as a directory
    kind = fs.check_path(self.root, newpath, self.taskpool)
    if kind != util.svn_node_dir:
      print "Path '%s' is not a valid filesystem directory." % newpath
      return
    self.path = newpath
    util.svn_pool_clear(self.taskpool)

  def cmd_ls(self, *args):
    """list the contents of the current directory or provided path"""
    args = args[0]
    parent = self.path
    if not len(args):
      # no args -- show a listing for the current directory.
      entries = fs.dir_entries(self.root, self.path, self.taskpool)
    else:
      # args?  show a listing of that path.
      newpath = self._parse_path(args[0])
      kind = fs.check_path(self.root, newpath, self.taskpool)
      if kind == util.svn_node_dir:
        parent = newpath
        entries = fs.dir_entries(self.root, parent, self.taskpool)
      elif kind == util.svn_node_file:
        parts = self._path_to_parts(newpath)
        name = parts.pop(-1)
        parent = self._parts_to_path(parts)
        print parent + ':' + name
        tmpentries = fs.dir_entries(self.root, parent, self.taskpool)
        if not tmpentries.get(name, None):
          return
        entries = {}
        entries[name] = tmpentries[name]
      else:
        print "Path '%s' not found." % newpath
        return
      
    keys = entries.keys()
    keys.sort()

    print "   REV   AUTHOR  NODE-REV-ID     SIZE         DATE NAME"
    print "----------------------------------------------------------------------------"

    for entry in keys:
      fullpath = parent + '/' + entry
      size = ''
      is_dir = fs.is_dir(self.root, fullpath, self.taskpool)
      if is_dir:
        name = entry + '/'
      else:
        size = str(fs.file_length(self.root, fullpath, self.taskpool))
        name = entry
      node_id = fs.unparse_id(fs.dirent_t_id_get(entries[entry]),
                              self.taskpool)
      created_rev = fs.node_created_rev(self.root, fullpath, self.taskpool)
      author = fs.revision_prop(self.fs_ptr, created_rev,
                                util.SVN_PROP_REVISION_AUTHOR, self.taskpool)
      if not author:
        author = ""
      date = fs.revision_prop(self.fs_ptr, created_rev,
                              util.SVN_PROP_REVISION_DATE, self.taskpool)
      if not date:
        date = ""
      else:
        date = self._format_date(date, self.taskpool)
     
      print "%6s %8s <%10s> %8s %12s %s" % (created_rev, author[:8],
                                            node_id, size, date, name)
    util.svn_pool_clear(self.taskpool)
  
  def cmd_lstxns(self, *args):
    """list the transactions available for browsing"""
    txns = fs.list_transactions(self.fs_ptr, self.taskpool)
    txns.sort()
    counter = 0
    for txn in txns:
      counter = counter + 1
      print "%8s  " % txn,
      if counter == 6:
        print ""
        counter = 0
    print ""
    util.svn_pool_clear(self.taskpool)
    
  def cmd_pcat(self, *args):
    """list the properties of a path"""
    args = args[0]
    catpath = self.path
    if args:
      catpath = self._parse_path(args[0])
    kind = fs.check_path(self.root, catpath, self.taskpool)
    if kind == util.svn_node_none:
      print "Path '%s' does not exist." % catpath
      return
    plist = fs.node_proplist(self.root, catpath, self.taskpool)
    if not plist:
      return
    for pkey, pval in plist.items():
      print 'K ' + str(len(pkey))
      print pkey
      print 'P ' + str(len(pval))
      print pval
    print 'PROPS-END'
    
  def cmd_setrev(self, *args):
    """set the current revision to view"""
    args = args[0]
    try:
      rev = int(args[0])
      newroot = fs.revision_root(self.fs_ptr, rev, self.pool)
    except:
      print "Error setting the revision to '" + str(rev) + "'."
      return
    fs.close_root(self.root)
    self.root = newroot
    self.rev = rev
    self.is_rev = 1
    self._do_path_landing()

  def cmd_settxn(self, *args):
    """set the current transaction to view"""
    args = args[0]
    txn = args[0]
    try:
      txnobj = fs.open_txn(self.fs_ptr, txn, self.pool)
      newroot = fs.txn_root(txnobj, self.pool)
    except:
      print "Error setting the transaction to '" + txn + "'."
      return
    fs.close_root(self.root)
    self.root = newroot
    self.txn = txn
    self.is_rev = 0
    self._do_path_landing()
  
  def cmd_youngest(self, *args):
    """list the youngest revision available for browsing"""
    rev = fs.youngest_rev(self.fs_ptr, self.taskpool)
    print rev
    util.svn_pool_clear(self.taskpool)

  def _path_to_parts(self, path):
    return filter(None, string.split(path, '/'))

  def _parts_to_path(self, parts):
    return '/' + string.join(parts, '/')

  def _parse_path(self, path):
    # cleanup leading, trailing, and duplicate '/' characters
    newpath = self._parts_to_path(self._path_to_parts(path))

    # if PATH is absolute, use it, else append it to the existing path.
    if path[0] == '/' or self.path == '/':
      newpath = '/' + newpath
    else:
      newpath = self.path + '/' + newpath

    # cleanup '.' and '..'
    parts = self._path_to_parts(newpath)
    finalparts = []
    for part in parts:
      if part == '.':
        pass
      elif part == '..':
        if len(finalparts) != 0:
          finalparts.pop(-1)
      else:
        finalparts.append(part)

    # finally, return the calculated path
    return self._parts_to_path(finalparts)
    
  def _format_date(self, date, pool):
    date = util.svn_time_from_cstring(date, pool)
    date = time.asctime(time.localtime(date / 1000000))
    return date[4:-8]
  
  def _do_path_landing(self):
    """try to land on self.path as a directory in root, failing up to '/'"""
    not_found = 1
    newpath = self.path
    while not_found:
      kind = fs.check_path(self.root, newpath, self.taskpool)
      if kind == util.svn_node_dir:
        not_found = 0
      else:
        parts = self._path_to_parts(newpath)
        parts.pop(-1)
        newpath = self._parts_to_path(parts)
    self.path = newpath
    util.svn_pool_clear(self.taskpool)

  _errors = ["Huh?",
             "Whatchoo talkin' 'bout, Willis?",
             "Say what?",
             "Nope.  Not gonna do it.",
             "Ehh...I don't think so, chief."]
  def _do_prompt(self):
    """present the prompt and handle the user's input"""
    if self.is_rev:
      prompt = "<rev: " + str(self.rev)
    else:
      prompt = "<txn: " + self.txn
    prompt += " " + self.path + ">$ "
    try:
      input = raw_input(prompt)
    except EOFError:
      print "" # generate a new-line before returning control
      return

    cmds = filter(None, string.split(input, ';'))
    for cmd in cmds:
      cmd.strip()
      
      ### This will currently screw up when the arguments to the
      ### commands have spaces in them, like 'cd "My Dir"'
      args = filter(None, string.split(cmd, ' '))
      if len(args) == 0:
        pass
      elif args[0] == 'exit':
        return
      elif not hasattr(self, 'cmd_' + args[0]):
        msg = self._errors[randint(0, len(self._errors) - 1)]
        print msg
      else:
        getattr(self, 'cmd_' + args[0])(args[1:])
        
    self._do_prompt()
    

def _basename(path):
  "Return the basename for a '/'-separated path."
  idx = string.rfind(path, '/')
  if idx == -1:
    return path
  return path[idx+1:]


def usage(exit):
  if exit:
    output = sys.stderr
  else:
    output = sys.stdout
  output.write(
    "usage: %s REPOS_PATH\n"
    "\n"
    "Once the program has started, type 'help' at the prompt for hints on\n"
    "using the shell.\n" % sys.argv[0])
  sys.exit(exit)

def main():
  if len(sys.argv) < 2:
    usage(1)

  try:
    # try to map the erase character to the backspace key
    import termios
    attrs = termios.tcgetattr(sys.stdin)
    attrs[6][termios.VERASE] = '\x08'
    termios.tcsetattr(sys.stdin, termios.TCSANOW, attrs)
  except:
    pass
  
  util.run_app(SVNShell, sys.argv[1])

if __name__ == '__main__':
  main()
