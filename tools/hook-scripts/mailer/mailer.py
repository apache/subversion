#!/usr/bin/env python2
#
# mailer.py: send email describing a commit
#
# $HeadURL$
# $LastChangedDate$
# $LastChangedBy$
# $LastChangedRevision$
#
# USAGE: mailer.py commit     REPOS-DIR REVISION [CONFIG-FILE]
#        mailer.py propchange REPOS-DIR REVISION AUTHOR PROPNAME [CONFIG-FILE]
#
#   Using CONFIG-FILE, deliver an email describing the changes between
#   REV and REV-1 for the repository REPOS.
#
#   This version of mailer.py requires the python bindings from
#   subversion 1.2.0 or later.
#

import os
import sys
import string
import ConfigParser
import time
import popen2
import cStringIO
import smtplib
import re
import tempfile
import types

import svn.fs
import svn.delta
import svn.repos
import svn.core

SEPARATOR = '=' * 78


def main(pool, cmd, config_fp, repos_dir, rev, author, propname, action):
  repos = Repository(repos_dir, rev, pool)

  if cmd == 'commit':
    cfg = Config(config_fp, repos, { 'author' : repos.author })
    messenger = Commit(pool, cfg, repos)
  elif cmd == 'propchange':
    # Override the repos revision author with the author of the propchange
    repos.author = author
    cfg = Config(config_fp, repos, { 'author' : author })
    messenger = PropChange(pool, cfg, repos, author, propname, action)
  else:
    raise UnknownSubcommand(cmd)

  messenger.generate()


# Minimal, incomplete, versions of popen2.Popen[34] for those platforms
# for which popen2 does not provide them.
try:
  Popen3 = popen2.Popen3
  Popen4 = popen2.Popen4
except AttributeError:
  class Popen3:
    def __init__(self, cmd, capturestderr = False):
      if type(cmd) != types.StringType:
        cmd = svn.core.argv_to_command_string(cmd)
      if capturestderr:
        self.fromchild, self.tochild, self.childerr \
            = popen2.popen3(cmd, mode='b')
      else:
        self.fromchild, self.tochild = popen2.popen2(cmd, mode='b')
        self.childerr = None

    def wait(self):
      rv = self.fromchild.close()
      rv = self.tochild.close() or rv
      if self.childerr is not None:
        rv = self.childerr.close() or rv
      return rv

  class Popen4:
    def __init__(self, cmd):
      if type(cmd) != types.StringType:
        cmd = svn.core.argv_to_command_string(cmd)
      self.fromchild, self.tochild = popen2.popen4(cmd, mode='b')

    def wait(self):
      rv = self.fromchild.close()
      rv = self.tochild.close() or rv
      return rv


class OutputBase:
  "Abstract base class to formalize the inteface of output methods"

  def __init__(self, cfg, repos, prefix_param):
    self.cfg = cfg
    self.repos = repos
    self.prefix_param = prefix_param
    self._CHUNKSIZE = 128 * 1024

    # This is a public member variable. This must be assigned a suitable
    # piece of descriptive text before make_subject() is called.
    self.subject = ""

  def make_subject(self, group, params):
    prefix = self.cfg.get(self.prefix_param, group, params)
    if prefix:
      subject = prefix + ' ' + self.subject
    else:
      subject = self.subject

    try:
      truncate_subject = int(
          self.cfg.get('truncate-subject', group, params))
    except ValueError:
      truncate_subject = 0

    if truncate_subject and len(subject) > truncate_subject:
      subject = subject[:(truncate_subject - 3)] + "..."
    return subject

  def start(self, group, params):
    """Override this method.
    Begin writing an output representation. GROUP is the name of the
    configuration file group which is causing this output to be produced.
    PARAMS is a dictionary of any named subexpressions of regular expressions
    defined in the configuration file, plus the key 'author' contains the
    author of the action being reported."""
    raise NotImplementedError

  def finish(self):
    """Override this method.
    Flush any cached information and finish writing the output
    representation."""
    raise NotImplementedError

  def write(self, output):
    """Override this method.
    Append the literal text string OUTPUT to the output representation."""
    raise NotImplementedError

  def run(self, cmd):
    """Override this method, if the default implementation is not sufficient.
    Execute CMD, writing the stdout produced to the output representation."""
    # By default we choose to incorporate child stderr into the output
    pipe_ob = Popen4(cmd)

    buf = pipe_ob.fromchild.read(self._CHUNKSIZE)
    while buf:
      self.write(buf)
      buf = pipe_ob.fromchild.read(self._CHUNKSIZE)

    # wait on the child so we don't end up with a billion zombies
    pipe_ob.wait()


class MailedOutput(OutputBase):
  def __init__(self, cfg, repos, prefix_param):
    OutputBase.__init__(self, cfg, repos, prefix_param)

  def start(self, group, params):
    # whitespace-separated list of addresses; split into a clean list:
    self.to_addrs = \
        filter(None, string.split(self.cfg.get('to_addr', group, params)))
    self.from_addr = self.cfg.get('from_addr', group, params) \
                     or self.repos.author or 'no_author'
    self.reply_to = self.cfg.get('reply_to', group, params)

  def mail_headers(self, group, params):
    subject = self.make_subject(group, params)
    hdrs = 'From: %s\n'    \
           'To: %s\n'      \
           'Subject: %s\n' \
           'MIME-Version: 1.0\n' \
           'Content-Type: text/plain; charset=UTF-8\n' \
           % (self.from_addr, string.join(self.to_addrs, ', '), subject)
    if self.reply_to:
      hdrs = '%sReply-To: %s\n' % (hdrs, self.reply_to)
    return hdrs + '\n'


class SMTPOutput(MailedOutput):
  "Deliver a mail message to an MTA using SMTP."

  def start(self, group, params):
    MailedOutput.start(self, group, params)

    self.buffer = cStringIO.StringIO()
    self.write = self.buffer.write

    self.write(self.mail_headers(group, params))

  def finish(self):
    server = smtplib.SMTP(self.cfg.general.smtp_hostname)
    if self.cfg.is_set('general.smtp_username'):
      server.login(self.cfg.general.smtp_username,
                   self.cfg.general.smtp_password)
    server.sendmail(self.from_addr, self.to_addrs, self.buffer.getvalue())
    server.quit()


class StandardOutput(OutputBase):
  "Print the commit message to stdout."

  def __init__(self, cfg, repos, prefix_param):
    OutputBase.__init__(self, cfg, repos, prefix_param)
    self.write = sys.stdout.write

  def start(self, group, params):
    self.write("Group: " + (group or "defaults") + "\n")
    self.write("Subject: " + self.make_subject(group, params) + "\n\n")

  def finish(self):
    pass


class PipeOutput(MailedOutput):
  "Deliver a mail message to an MDA via a pipe."

  def __init__(self, cfg, repos, prefix_param):
    MailedOutput.__init__(self, cfg, repos, prefix_param)

    # figure out the command for delivery
    self.cmd = string.split(cfg.general.mail_command)

  def start(self, group, params):
    MailedOutput.start(self, group, params)

    ### gotta fix this. this is pretty specific to sendmail and qmail's
    ### mailwrapper program. should be able to use option param substitution
    cmd = self.cmd + [ '-f', self.from_addr ] + self.to_addrs

    # construct the pipe for talking to the mailer
    self.pipe = Popen3(cmd)
    self.write = self.pipe.tochild.write

    # we don't need the read-from-mailer descriptor, so close it
    self.pipe.fromchild.close()

    # start writing out the mail message
    self.write(self.mail_headers(group, params))

  def finish(self):
    # signal that we're done sending content
    self.pipe.tochild.close()

    # wait to avoid zombies
    self.pipe.wait()


class Messenger:
  def __init__(self, pool, cfg, repos, prefix_param):
    self.pool = pool
    self.cfg = cfg
    self.repos = repos

    if cfg.is_set('general.mail_command'):
      cls = PipeOutput
    elif cfg.is_set('general.smtp_hostname'):
      cls = SMTPOutput
    else:
      cls = StandardOutput

    self.output = cls(cfg, repos, prefix_param)


class Commit(Messenger):
  def __init__(self, pool, cfg, repos):
    Messenger.__init__(self, pool, cfg, repos, 'commit_subject_prefix')

    # get all the changes and sort by path
    editor = svn.repos.ChangeCollector(repos.fs_ptr, repos.root_this, self.pool)
    e_ptr, e_baton = svn.delta.make_editor(editor, self.pool)
    svn.repos.replay(repos.root_this, e_ptr, e_baton, self.pool)

    self.changelist = editor.get_changes().items()
    self.changelist.sort()

    # collect the set of groups and the unique sets of params for the options
    self.groups = { }
    for path, change in self.changelist:
      for (group, params) in self.cfg.which_groups(path):
        # turn the params into a hashable object and stash it away
        param_list = params.items()
        param_list.sort()
        # collect the set of paths belonging to this group
        if self.groups.has_key( (group, tuple(param_list)) ):
          old_param, paths = self.groups[group, tuple(param_list)]
        else:
          paths = { }
        paths[path] = None
        self.groups[group, tuple(param_list)] = (params, paths)

    # figure out the changed directories
    dirs = { }
    for path, change in self.changelist:
      if change.item_kind == svn.core.svn_node_dir:
        dirs[path] = None
      else:
        idx = string.rfind(path, '/')
        if idx == -1:
          dirs[''] = None
        else:
          dirs[path[:idx]] = None

    dirlist = dirs.keys()

    # figure out the common portion of all the dirs. note that there is
    # no "common" if only a single dir was changed, or the root was changed.
    if len(dirs) == 1 or dirs.has_key(''):
      commondir = ''
    else:
      common = string.split(dirlist.pop(), '/')
      for d in dirlist:
        parts = string.split(d, '/')
        for i in range(len(common)):
          if i == len(parts) or common[i] != parts[i]:
            del common[i:]
            break
      commondir = string.join(common, '/')
      if commondir:
        # strip the common portion from each directory
        l = len(commondir) + 1
        dirlist = [ ]
        for d in dirs.keys():
          if d == commondir:
            dirlist.append('.')
          else:
            dirlist.append(d[l:])
      else:
        # nothing in common, so reset the list of directories
        dirlist = dirs.keys()

    # compose the basic subject line. later, we can prefix it.
    dirlist.sort()
    dirlist = string.join(dirlist)
    if commondir:
      self.output.subject = 'r%d - in %s: %s' % (repos.rev, commondir, dirlist)
    else:
      self.output.subject = 'r%d - %s' % (repos.rev, dirlist)

  def generate(self):
    "Generate email for the various groups and option-params."

    ### the groups need to be further compressed. if the headers and
    ### body are the same across groups, then we can have multiple To:
    ### addresses. SMTPOutput holds the entire message body in memory,
    ### so if the body doesn't change, then it can be sent N times
    ### rather than rebuilding it each time.

    subpool = svn.core.svn_pool_create(self.pool)

    for (group, param_tuple), (params, paths) in self.groups.items():
      self.output.start(group, params)

      # generate the content for this group and set of params
      generate_content(self.output, self.cfg, self.repos, self.changelist,
                       group, params, paths, subpool)

      self.output.finish()
      svn.core.svn_pool_clear(subpool)

    svn.core.svn_pool_destroy(subpool)


try:
  from tempfile import NamedTemporaryFile
except ImportError:
  # NamedTemporaryFile was added in Python 2.3, so we need to emulate it
  # for older Pythons.
  class NamedTemporaryFile:
    def __init__(self):
      self.name = tempfile.mktemp()
      self.file = open(self.name, 'w+b')
    def __del__(self):
      os.remove(self.name)
    def write(self, data):
      self.file.write(data)
    def flush(self):
      self.file.flush()


class PropChange(Messenger):
  def __init__(self, pool, cfg, repos, author, propname, action):
    Messenger.__init__(self, pool, cfg, repos, 'propchange_subject_prefix')
    self.author = author
    self.propname = propname
    self.action = action

    # collect the set of groups and the unique sets of params for the options
    self.groups = { }
    for (group, params) in self.cfg.which_groups(''):
      # turn the params into a hashable object and stash it away
      param_list = params.items()
      param_list.sort()
      self.groups[group, tuple(param_list)] = params

    self.output.subject = 'r%d - %s' % (repos.rev, propname)

  def generate(self):
    actions = { 'A': 'added', 'M': 'modified', 'D': 'deleted' }
    for (group, param_tuple), params in self.groups.items():
      self.output.start(group, params)
      self.output.write('Author: %s\n'
                        'Revision: %s\n'
                        'Property Name: %s\n'
                        'Action: %s\n'
                        '\n'
                        % (self.author, self.repos.rev, self.propname,
                           actions.get(action, 'Unknown (\'%s\')' % action)))
      if action == 'A':
        self.output.write('Property value:\n')
        propvalue = self.repos.get_rev_prop(self.propname)
        self.output.write(propvalue)
      elif action != 'D':
        self.output.write('Property diff:\n')
        tempfile1 = NamedTemporaryFile()
        tempfile1.write(sys.stdin.read())
        tempfile1.flush()
        tempfile2 = NamedTemporaryFile()
        tempfile2.write(self.repos.get_rev_prop(self.propname))
        tempfile2.flush()
        self.output.run(self.cfg.get_diff_cmd(group, {
          'label_from' : 'old property value',
          'label_to' : 'new property value',
          'from' : tempfile1.name,
          'to' : tempfile2.name,
          }))
      self.output.finish()


class DiffSelections:
  def __init__(self, cfg, group, params):
    self.add = False
    self.copy = False
    self.delete = False
    self.modify = False

    gen_diffs = cfg.get('generate_diffs', group, params)

    ### Do a little dance for deprecated options.  Note that even if you
    ### don't have an option anywhere in your configuration file, it
    ### still gets returned as non-None.
    if len(gen_diffs):
      list = string.split(gen_diffs, " ")
      for item in list:
        if item == 'add':
          self.add = True
        if item == 'copy':
          self.copy = True
        if item == 'delete':
          self.delete = True
        if item == 'modify':
          self.modify = True
    else:
      self.add = True
      self.copy = True
      self.delete = True
      self.modify = True
      ### These options are deprecated
      suppress = cfg.get('suppress_deletes', group, params)
      if suppress == 'yes':
        self.delete = False
      suppress = cfg.get('suppress_adds', group, params)
      if suppress == 'yes':
        self.add = False


def generate_content(output, cfg, repos, changelist, group, params, paths,
                     pool):

  svndate = repos.get_rev_prop(svn.core.SVN_PROP_REVISION_DATE)
  ### pick a different date format?
  date = time.ctime(svn.core.secs_from_timestr(svndate, pool))

  diffsels = DiffSelections(cfg, group, params)

  output.write('Author: %s\nDate: %s\nNew Revision: %s\n\n'
               % (repos.author, date, repos.rev))

  excluded_paths = cfg.get('excluded_paths', group, params) or 'show-all'

  # print summary sections
  # first, those changes within the selected path-space
  generate_list(output, 'A', changelist, paths, True)
  generate_list(output, 'R', changelist, paths, True)
  generate_list(output, 'M', changelist, paths, True)

  # second, those outside, if any
  if len(paths) != len(changelist):
    if excluded_paths == 'hide':
      output.write('and changes in other areas\n')
    else:
      output.write('\nChanges in other areas also in this revision:\n')
      generate_list(output, 'A', changelist, paths, False)
      generate_list(output, 'R', changelist, paths, False)
      generate_list(output, 'M', changelist, paths, False)

  output.write('\nLog:\n%s\n'
               % (repos.get_rev_prop(svn.core.SVN_PROP_REVISION_LOG) or ''))

  # these are sorted by path already
  for path, change in changelist:
    if paths.has_key(path):
      generate_diff(output, cfg, repos, date, path, change, group, params,
                    diffsels, pool)

  if len(paths) != len(changelist) and excluded_paths == 'show-all':
    output.write('\nDiffs of changes in other areas also in this revision:\n')
    for path, change in changelist:
      if not paths.has_key(path):
        generate_diff(output, cfg, repos, date, path, change, group, params,
                      diffsels, pool)


def generate_list(output, changekind, changelist, paths, in_paths):
  items = [ ]
  if changekind == 'A':
    header = 'Added'
    selection = lambda change: change.added
  elif changekind == 'R':
    header = 'Removed'
    selection = lambda change: change.path is None
  elif changekind == 'M':
    header = 'Modified'
    selection = lambda change: not change.added and change.path is not None

  for path, change in changelist:
    if selection(change) and (paths.has_key(path) == in_paths):
      items.append((path, change))
  if items:
    output.write('%s:\n' % header)
    for fname, change in items:
      if change.item_kind == svn.core.svn_node_dir:
        is_dir = '/'
      else:
        is_dir = ''
      if change.prop_changes:
        if change.text_changed:
          props = '   (contents, props changed)'
        else:
          props = '   (props changed)'
      else:
        props = ''
      output.write('   %s%s%s\n' % (fname, is_dir, props))
      if change.added and change.base_path:
        if is_dir:
          text = ''
        elif change.text_changed:
          text = ', changed'
        else:
          text = ' unchanged'
        output.write('      - copied%s from r%d, %s%s\n'
                     % (text, change.base_rev, change.base_path, is_dir))


def generate_diff(output, cfg, repos, date, change, group, params,
                  diffsels, pool):
  if change.item_kind == svn.core.svn_node_dir:
    # all changes were printed in the summary. nothing to do.
    return

  if not change.path:
    ### params is a bit silly here
    if not diffsels.delete:
      # a record of the deletion is in the summary. no need to write
      # anything further here.
      return

    output.write('\nDeleted: %s\n' % change.base_path)
    diff = svn.fs.FileDiff(repos.get_root(change.base_rev),
                           change.base_path, None, None, pool)

    label1 = '%s\t%s' % (change.base_path, date)
    label2 = '(empty file)'
    singular = True
  elif change.added:
    if change.base_path and (change.base_rev != -1):
      # this file was copied.

      if not change.text_changed:
        # copies with no changes are reported in the header, so we can just
        # skip them here.
        return

      if not diffsels.copy:
        # a record of the copy is in the summary, no need to write
        # anything further here.
	return

      # note that we strip the leading slash from the base (copyfrom) path
      output.write('\nCopied: %s (from r%d, %s)\n'
                   % (change.path, change.base_rev, change.base_path))
      diff = svn.fs.FileDiff(repos.get_root(change.base_rev),
                             change.base_path,
                             repos.root_this, change.path,
                             pool)
      label1 = change.base_path + '\t(original)'
      label2 = '%s\t%s' % (change.path, date)
      singular = False
    else:
      if not diffsels.add:
        # a record of the addition is in the summary. no need to write
        # anything further here.
        return

      output.write('\nAdded: %s\n' % change.path)
      diff = svn.fs.FileDiff(None, None, repos.root_this, change.path, pool)
      label1 = '(empty file)'
      label2 = '%s\t%s' % (change.path, date)
      singular = True
  elif not change.text_changed:
    # don't bother to show an empty diff. prolly just a prop change.
    return
  else:
    if not diffsels.modify:
      # a record of the modification is in the summary, no need to write
      # anything further here.
      return

    output.write('\nModified: %s\n' % change.path)
    diff = svn.fs.FileDiff(repos.get_root(change.base_rev),
                           change.base_path,
                           repos.root_this, change.path,
                           pool)
    label1 = change.base_path + '\t(original)'
    label2 = '%s\t%s' % (change.path, date)
    singular = False

  output.write(SEPARATOR + '\n')

  if diff.either_binary():
    if singular:
      output.write('Binary file. No diff available.\n')
    else:
      output.write('Binary files. No diff available.\n')
    return

  ### do something with change.prop_changes

  src_fname, dst_fname = diff.get_files()

  output.run(cfg.get_diff_cmd(group, {
    'label_from' : label1,
    'label_to' : label2,
    'from' : src_fname,
    'to' : dst_fname,
    }))


class Repository:
  "Hold roots and other information about the repository."

  def __init__(self, repos_dir, rev, pool):
    self.repos_dir = repos_dir
    self.rev = rev
    self.pool = pool

    self.repos_ptr = svn.repos.open(repos_dir, pool)
    self.fs_ptr = svn.repos.fs(self.repos_ptr)

    self.roots = { }

    self.root_this = self.get_root(rev)

    self.author = self.get_rev_prop(svn.core.SVN_PROP_REVISION_AUTHOR)

  def get_rev_prop(self, propname):
    return svn.fs.revision_prop(self.fs_ptr, self.rev, propname, self.pool)

  def get_root(self, rev):
    try:
      return self.roots[rev]
    except KeyError:
      pass
    root = self.roots[rev] = svn.fs.revision_root(self.fs_ptr, rev, self.pool)
    return root


class Config:

  # The predefined configuration sections. These are omitted from the
  # set of groups.
  _predefined = ('general', 'defaults', 'maps')

  def __init__(self, fp, repos, global_params):
    cp = ConfigParser.ConfigParser()
    cp.readfp(fp)

    # record the (non-default) groups that we find
    self._groups = [ ]

    for section in cp.sections():
      if not hasattr(self, section):
        section_ob = _sub_section()
        setattr(self, section, section_ob)
        if section not in self._predefined:
          self._groups.append(section)
      else:
        section_ob = getattr(self, section)
      for option in cp.options(section):
        # get the raw value -- we use the same format for *our* interpolation
        value = cp.get(section, option, raw=1)
        setattr(section_ob, option, value)

    # be compatible with old format config files
    if hasattr(self.general, 'diff') and not hasattr(self.defaults, 'diff'):
      self.defaults.diff = self.general.diff

    # these params are always available, although they may be overridden
    self._global_params = global_params.copy()

    # prepare maps. this may remove sections from consideration as a group.
    self._prep_maps()

    # process all the group sections.
    self._prep_groups(repos)

  def is_set(self, option):
    """Return None if the option is not set; otherwise, its value is returned.

    The option is specified as a dotted symbol, such as 'general.mail_command'
    """
    ob = self
    for part in string.split(option, '.'):
      if not hasattr(ob, part):
        return None
      ob = getattr(ob, part)
    return ob

  def get(self, option, group, params):
    "Get a config value with appropriate substitutions and value mapping."

    # find the right value
    value = None
    if group:
      sub = getattr(self, group)
      value = getattr(sub, option, None)
    if value is None:
      value = getattr(self.defaults, option, '')
    
    # parameterize it
    if params is not None:
      value = value % params

    # apply any mapper
    mapper = getattr(self.maps, option, None)
    if mapper is not None:
      value = mapper(value)

    return value

  def get_diff_cmd(self, group, args):
    "Get a diff command as a list of argv elements."
    ### do some better splitting to enable quoting of spaces
    diff_cmd = string.split(self.get('diff', group, None))

    cmd = [ ]
    for part in diff_cmd:
      cmd.append(part % args)
    return cmd

  def _prep_maps(self):
    "Rewrite the [maps] options into callables that look up values."

    for optname, mapvalue in vars(self.maps).items():
      if mapvalue[:1] == '[':
        # a section is acting as a mapping
        sectname = mapvalue[1:-1]
        if not hasattr(self, sectname):
          raise UnknownMappingSection(sectname)
        # construct a lambda to look up the given value as an option name,
        # and return the option's value. if the option is not present,
        # then just return the value unchanged.
        setattr(self.maps, optname,
                lambda value,
                       sect=getattr(self, sectname): getattr(sect, value,
                                                             value))
        # remove the mapping section from consideration as a group
        self._groups.remove(sectname)

      # elif test for other mapper types. possible examples:
      #   dbm:filename.db
      #   file:two-column-file.txt
      #   ldap:some-query-spec
      # just craft a mapper function and insert it appropriately

      else:
        raise UnknownMappingSpec(mapvalue)

  def _prep_groups(self, repos):
    self._group_re = [ ]

    repos_dir = os.path.abspath(repos.repos_dir)

    # compute the default repository-based parameters. start with some
    # basic parameters, then bring in the regex-based params.
    default_params = self._global_params.copy()

    try:
      match = re.match(self.defaults.for_repos, repos_dir)
      if match:
        default_params.update(match.groupdict())
    except AttributeError:
      # there is no self.defaults.for_repos
      pass

    # select the groups that apply to this repository
    for group in self._groups:
      sub = getattr(self, group)
      params = default_params
      if hasattr(sub, 'for_repos'):
        match = re.match(sub.for_repos, repos_dir)
        if not match:
          continue
        params = self._global_params.copy()
        params.update(match.groupdict())

      # if a matching rule hasn't been given, then use the empty string
      # as it will match all paths
      for_paths = getattr(sub, 'for_paths', '')
      exclude_paths = getattr(sub, 'exclude_paths', None)
      if exclude_paths:
        exclude_paths_re = re.compile(exclude_paths)
      else:
        exclude_paths_re = None

      self._group_re.append((group, re.compile(for_paths),
                             exclude_paths_re, params))

    # after all the groups are done, add in the default group
    try:
      self._group_re.append((None,
                             re.compile(self.defaults.for_paths),
                             None,
                             default_params))
    except AttributeError:
      # there is no self.defaults.for_paths
      pass

  def which_groups(self, path):
    "Return the path's associated groups."
    groups = []
    for group, pattern, exclude_pattern, repos_params in self._group_re:
      match = pattern.match(path)
      if match:
        if exclude_pattern and exclude_pattern.match(path):
          continue
        params = repos_params.copy()
        params.update(match.groupdict())
        groups.append((group, params))
    if not groups:
      groups.append((None, self._global_params))
    return groups


class _sub_section:
  pass


class MissingConfig(Exception):
  pass
class UnknownMappingSection(Exception):
  pass
class UnknownMappingSpec(Exception):
  pass
class UnknownSubcommand(Exception):
  pass


# enable True/False in older vsns of Python
try:
  _unused = True
except NameError:
  True = 1
  False = 0


if __name__ == '__main__':
  def usage():
    sys.stderr.write(
"""USAGE: %s commit     REPOS-DIR REVISION [CONFIG-FILE]
       %s propchange REPOS-DIR REVISION AUTHOR PROPNAME ACTION [CONFIG-FILE]

If no CONFIG-FILE is provided, the script will first search for a mailer.conf
file in REPOS-DIR/conf/.  Failing that, it will search the directory in which
the script itself resides.

Additionally, CONFIG-FILE may be '-' to indicate that configuration options
will be provided via standard input.

""" % (sys.argv[0], sys.argv[0]))
    sys.exit(1)

  if len(sys.argv) < 4:
    usage()

  cmd = sys.argv[1]
  repos_dir = sys.argv[2]
  revision = int(sys.argv[3])
  config_fname = None

  # Used for propchange only
  author = None
  propname = None
  action = None

  if cmd == 'commit':
    if len(sys.argv) > 5:
      usage()
    if len(sys.argv) > 4:
      config_fname = sys.argv[4]
  elif cmd == 'propchange':
    if len(sys.argv) < 6 or len(sys.argv) > 8:
      usage()
    author = sys.argv[4]
    propname = sys.argv[5]
    action = sys.argv[6]
    if len(sys.argv) > 7:
      config_fname = sys.argv[7]
  else:
    usage()

  if config_fname is None:
    # default to REPOS-DIR/conf/mailer.conf
    config_fname = os.path.join(repos_dir, 'conf', 'mailer.conf')
    if not os.path.exists(config_fname):
      # okay. look for 'mailer.conf' as a sibling of this script
      config_fname = os.path.join(os.path.dirname(sys.argv[0]), 'mailer.conf')

  # If we're supposed to read from stdin, do so.
  if config_fname == '-':
    config_fp = sys.stdin
  else:
    # Not reading stdin, so open up the real config file.
    if not os.path.exists(config_fname):
      raise MissingConfig(config_fname)
    config_fp = open(config_fname)

  ### run some validation on these params
  svn.core.run_app(main, cmd, config_fp, repos_dir, revision,
                   author, propname, action)

# ------------------------------------------------------------------------
# TODO
#
# * add configuration options
#   - each group defines delivery info:
#     o whether to set Reply-To and/or Mail-Followup-To
#       (btw: it is legal do set Reply-To since this is the originator of the
#        mail; i.e. different from MLMs that munge it)
#   - each group defines content construction:
#     o max size of diff before trimming
#     o max size of entire commit message before truncation
#   - per-repository configuration
#     o extra config living in repos
#     o how to construct a ViewCVS URL for the diff  [DONE (as patch)]
#     o optional, non-mail log file
#     o look up authors (username -> email; for the From: header) in a
#       file(s) or DBM
#   - if the subject line gets too long, then trim it. configurable?
# * get rid of global functions that should properly be class methods
