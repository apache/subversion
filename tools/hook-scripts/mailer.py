#!/usr/bin/env python2
#
# mailer.py: send email describing a commit
#
# USAGE: mailer.py REPOS-DIR REVISION [CONFIG-FILE]
#
#   Using CONFIG-FILE, deliver an email describing the changes between
#   REV and REV-1 for the repository REPOS.
#

import os
import sys
import string
import ConfigParser
import time

import svn.fs
import svn.util
import svn.delta
import svn.repos

SEPARATOR = '=' * 78

def main(pool, config_fname, repos_dir, rev):
  cfg = Config(config_fname)

  repos = Repository(repos_dir, rev, pool)

  editor = ChangeCollector(repos.root_prev)

  e_ptr, e_baton = svn.delta.make_editor(editor, pool)
  svn.repos.svn_repos_dir_delta(repos.root_prev, '', None, repos.root_this, '',
                                e_ptr, e_baton,
                                0,  # text_deltas
                                1,  # recurse
                                0,  # entry_props
                                1,  # use_copy_history
                                pool)

  output = determine_output(cfg, repos, editor.changes)
  generate_content(output, cfg, repos, editor.changes, pool)
  output.finish()


def determine_output(cfg, repos, changes):
  ### process changes to determine the applicable groups

  ### selecting SMTP will be more than this, but this'll do for now
  if hasattr(cfg.general, 'smtp_hostname'):
    return SMTPOutput(cfg)

  return StandardOutput()


class SMTPOutput:
  def __init__(self, cfg):
    import cStringIO
    self.cfg = cfg
    self.buffer = cStringIO.StringIO()
    self.write = self.buffer.write

  def finish(self):
    import smtplib
    server = smtplib.SMTP(self.cfg.general.smtp_hostname)

    ### we need to set some headers before dumping in the content
    server.sendmail(self.cfg.general.from_addr,
                    [ self.cfg.general.to_addr ],
                    self.buffer.getvalue())
    server.quit()

class StandardOutput:
  def __init__(self):
    self.write = sys.stdout.write

  def finish(self):
    pass


def generate_content(output, cfg, repos, changes, pool):

  svndate = repos.get_rev_prop(svn.util.SVN_PROP_REVISION_DATE)
  ### pick a different date format?
  date = time.ctime(svn.util.secs_from_timestr(svndate, pool))

  output.write('Author: %s\nDate: %s\nNew Revision: %s\n\n'
               % (repos.get_rev_prop(svn.util.SVN_PROP_REVISION_AUTHOR),
                  date,
                  repos.rev))

  # get all the changes and sort by path
  changelist = changes.items()
  changelist.sort()

  # print summary sections
  generate_list(output, 'Added', changelist, _select_adds)
  generate_list(output, 'Removed', changelist, _select_deletes)
  generate_list(output, 'Modified', changelist, _select_modifies)

  output.write('Log:\n%s\n'
               % (repos.get_rev_prop(svn.util.SVN_PROP_REVISION_LOG) or ''))

  # these are sorted by path already
  for path, change in changelist:
    generate_diff(output, cfg, repos, date, change, pool)

  ### print diffs. watch for binary files.


def _select_adds(change):
  return change.added
def _select_deletes(change):
  return not change.path
def _select_modifies(change):
  return not change.added and change.path


def generate_list(output, header, changelist, selection):
  items = [ ]
  for path, change in changelist:
    if selection(change):
      items.append((path, change))
  if items:
    output.write('%s:\n' % header)
    for fname, change in items:
      ### should print prop_changes?, copy_info, and binary? here
      ### hmm. don't have binary right now.
      if change.item_type == ChangeCollector.DIR:
        output.write('   %s/\n' % fname)
      else:
        output.write('   %s\n' % fname)


def generate_diff(output, cfg, repos, date, change, pool):

  if change.item_type == ChangeCollector.DIR:
    if change.path and change.base_path:
      output.write('\nCopied: %s (from rev %d, %s)\n'
                   % (change.path, change.base_rev, change.base_path[1:]))
    ### do we want to display anything for a directory? probably... if
    ### the thing has properties, then heck ya.
    return

  if not change.path:
    output.write('Deleted: %s\n' % change.base_path)
    diff = svn.fs.FileDiff(repos.root_prev, change.base_path, None, None, pool)

    label1 = '%s\t%s' % (change.base_path, date)
    label2 = '(empty file)'
  elif change.added:
    if change.base_path:
      # this was copied. note that we strip the leading slash from the
      # base (copyfrom) path.
      output.write('Copied: %s (from rev %d, %s)\n'
                   % (change.path, change.base_rev, change.base_path[1:]))
      diff = svn.fs.FileDiff(repos.get_root(change.base_rev),
                             change.base_path[1:],
                             repos.root_this, change.path,
                             pool)
      label1 = change.base_path[1:] + '\t(original)'
      label2 = '%s\t%s' % (change.path, date)
    else:
      output.write('Added: %s\n' % change.path)
      diff = svn.fs.FileDiff(None, None, repos.root_this, change.path, pool)
      label1 = '(empty file)'
      label2 = '%s\t%s' % (change.path, date)
  else:
    output.write('\nModified: %s\n' % change.path)
    diff = svn.fs.FileDiff(repos.get_root(change.base_rev), change.base_path[1:],
                           repos.root_this, change.path,
                           pool)
    label1 = change.base_path[1:] + '\t(original)'
    label2 = '%s\t%s' % (change.path, date)

  output.write(SEPARATOR + '\n')

  ### do something with change.prop_changes

  src_fname, dst_fname = diff.get_files()

  pipe = os.popen(cfg.general.diff % {
    'label_from' : label1,
    'label_to' : label2,
    'from' : src_fname,
    'to' : dst_fname,
    })
  while 1:
    chunk = pipe.read(100000)
    if not chunk:
      break
    output.write(chunk)


class Repository:
  "Hold roots and other information about the repository."

  def __init__(self, repos_dir, rev, pool):
    self.repos_dir = repos_dir
    self.rev = rev
    self.pool = pool

    db_path = os.path.join(repos_dir, 'db')
    if not os.path.exists(db_path):
      db_path = repos_dir

    self.fs_ptr = svn.fs.new(pool)
    svn.fs.open_berkeley(self.fs_ptr, db_path)

    self.roots = { }

    self.root_prev = self.get_root(rev-1)
    self.root_this = self.get_root(rev)

  def get_rev_prop(self, propname):
    return svn.fs.revision_prop(self.fs_ptr, self.rev, propname, self.pool)

  def get_root(self, rev):
    try:
      return self.roots[rev]
    except KeyError:
      pass
    root = self.roots[rev] = svn.fs.revision_root(self.fs_ptr, rev, self.pool)
    return root


class ChangeCollector(svn.delta.Editor):
  DIR = 'DIR'
  FILE = 'FILE'

  def __init__(self, root_prev):
    self.root_prev = root_prev

    # path -> _change()
    self.changes = { }

  def open_root(self, base_revision, dir_pool):
    return ('', '', base_revision)  # dir_baton

  def delete_entry(self, path, revision, parent_baton, pool):
    if svn.fs.is_dir(self.root_prev, '/' + path, pool):
      item_type = ChangeCollector.DIR
    else:
      item_type = ChangeCollector.FILE
    # base_path is the specified path. revision is the parent's.
    self.changes[path] = _change(item_type,
                                 False,
                                 path,            # base_path
                                 parent_baton[2], # base_rev
                                 None,            # (new) path
                                 False,           # added
                                 )

  def add_directory(self, path, parent_baton,
                    copyfrom_path, copyfrom_revision, dir_pool):
    self.changes[path] = _change(ChangeCollector.DIR,
                                 False,
                                 copyfrom_path,     # base_path
                                 copyfrom_revision, # base_rev
                                 path,              # path
                                 True,              # added
                                 )

    return (path, copyfrom_path, copyfrom_revision)  # dir_baton

  def open_directory(self, path, parent_baton, base_revision, dir_pool):
    assert parent_baton[2] == base_revision

    base_path = _svn_join(parent_baton[1], _svn_basename(path))
    return (path, base_path, base_revision)  # dir_baton

  def change_dir_prop(self, dir_baton, name, value, pool):
    dir_path = dir_baton[0]
    if self.changes.has_key(dir_path):
      self.changes[dir_path].prop_changes = True
    else:
      # can't be added or deleted, so this must be CHANGED
      self.changes[dir_path] = _change(ChangeCollector.DIR,
                                       True,
                                       dir_baton[1], # base_path
                                       dir_baton[2], # base_rev
                                       dir_path,     # path
                                       False,        # added
                                       )

  def add_file(self, path, parent_baton,
               copyfrom_path, copyfrom_revision, file_pool):
    self.changes[path] = _change(ChangeCollector.FILE,
                                 False,
                                 copyfrom_path,     # base_path
                                 copyfrom_revision, # base_rev
                                 path,              # path
                                 True,              # added
                                 )

    return (path, copyfrom_path, copyfrom_revision)  # file_baton

  def open_file(self, path, parent_baton, base_revision, file_pool):
    assert parent_baton[2] == base_revision

    base_path = _svn_join(parent_baton[1], _svn_basename(path))
    return (path, base_path, base_revision)  # file_baton

  def apply_textdelta(self, file_baton):
    file_path = file_baton[0]
    if not self.changes.has_key(file_path):
      # an add would have inserted a change record already, and it can't
      # be a delete with a text delta, so this must be a normal change.
      self.changes[file_path] = _change(ChangeCollector.FILE,
                                        False,
                                        file_baton[1], # base_path
                                        file_baton[2], # base_rev
                                        file_path,     # path
                                        False,         # added
                                        )

    # no handler
    return None

  def change_file_prop(self, file_baton, name, value, pool):
    file_path = file_baton[0]
    if self.changes.has_key(file_path):
      self.changes[file_path].prop_changes = True
    else:
      # an add would have inserted a change record already, and it can't
      # be a delete with a prop change, so this must be a normal change.
      self.changes[file_path] = _change(ChangeCollector.FILE,
                                        True,
                                        file_baton[1], # base_path
                                        file_baton[2], # base_rev
                                        file_path,     # path
                                        False,         # added
                                        )


class _change:
  __slots__ = [ 'item_type', 'prop_changes',
                'base_path', 'base_rev', 'path',
                'added',
                ]
  def __init__(self,
               item_type, prop_changes, base_path, base_rev, path, added):
    self.item_type = item_type
    self.prop_changes = prop_changes
    self.base_path = base_path
    self.base_rev = base_rev
    self.path = path

    ### it would be nice to avoid this flag. however, without it, it would
    ### be quite difficult to distinguish between a change to the previous
    ### revision (which has a base_path/base_rev) and a copy from some
    ### other path/rev. a change in path is obviously add-with-history,
    ### but the same path could be a change to the previous rev or a restore
    ### of an older version. when it is "change to previous", I'm not sure
    ### if the rev is always repos.rev - 1, or whether it represents the
    ### created or time-of-checkou rev. so... we use a flag (for now)
    self.added = added


class Config:
  def __init__(self, fname):
    cp = ConfigParser.ConfigParser()
    cp.read(fname)

    for section in cp.sections():
      if not hasattr(self, section):
        setattr(self, section, _sub_section())
      section_ob = getattr(self, section)
      for option in cp.options(section):
        # get the raw value -- we use the same format for *our* interpolation
        value = cp.get(section, option, raw=1)
        setattr(section_ob, option, value)


class _sub_section:
  pass


class MissingConfig(Exception):
  pass


def _svn_basename(path):
  "Compute the basename of an SVN path ('/' separators)."
  idx = string.rfind(path, '/')
  if idx == -1:
    return path
  return path[idx+1:]

def _svn_join(base, relative):
  "Join a relative path onto a base path using the SVN separator ('/')."
  if relative[:1] == '/':
    return relative
  if base[-1:] == '/':
    return base + relative
  return base + '/' + relative


# enable True/False in older vsns of Python
try:
  _unused = True
except NameError:
  True = 1
  False = 0


if __name__ == '__main__':
  if len(sys.argv) < 3 or len(sys.argv) > 4:
    sys.stderr.write('USAGE: %s REPOS-DIR REVISION [CONFIG-FILE]\n'
                     % sys.argv[0])
    sys.exit(1)

  repos_dir = sys.argv[1]
  revision = int(sys.argv[2])

  if len(sys.argv) == 3:
    # default to REPOS-DIR/conf/mailer.conf
    config_fname = os.path.join(repos_dir, 'conf', 'mailer.conf')
    if not os.path.exists(config_fname):
      # okay. look for 'mailer.conf' as a sibling of this script
      config_fname = os.path.join(os.path.dirname(sys.argv[0]), 'mailer.conf')
  else:
    # the config file was explicitly provided
    config_fname = sys.argv[3]

  if not os.path.exists(config_fname):
    raise MissingConfig(config_fname)

  ### run some validation on these params
  svn.util.run_app(main, config_fname, repos_dir, revision)


# ------------------------------------------------------------------------
# TODO
#
# * pipe output into a mail program (and avoid os.popen)
# * add configuration options
#   - default options
#   - per-group overrides
#   - group selection based on repos and on path
#   - each group defines:
#     o how to construct From:
#     o how to construct To:
#     o whether to set Reply-To and/or Mail-Followup-To
#       (btw: it is legal do set Reply-To since this is the originator of the
#        mail; i.e. different from MLMs that munge it)
#     o max size of diff before trimming
#     o how to construct a ViewCVS URL for the diff
#     o optional, non-mail log file
#     o flag to disable generation of add/delete diffs
#
