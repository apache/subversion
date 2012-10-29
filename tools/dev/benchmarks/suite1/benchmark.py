#!/usr/bin/env python

# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing,
# software distributed under the License is distributed on an
# "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
# KIND, either express or implied.  See the License for the
# specific language governing permissions and limitations
# under the License.

"""Usage: benchmark.py run|list|compare|show|chart ...

RUN BENCHMARKS

  benchmark.py run <branch>@<revision>,<levels>x<spread> [N] [options]

Test data is added to an sqlite database created automatically, by default
'benchmark.db' in the current working directory. To specify a different path,
use option -f <path_to_db>.

<branch_name> is a label of the svn branch you're testing, e.g. "1.7.x".
<revision> is the last-changed-revision of above branch.
<levels> is the number of directory levels to create
<spread> is the number of child trees spreading off each dir level
If <N> is provided, the run is repeated N times.

<branch_name> and <revision> are simply used for later reference. You
should enter labels matching the selected --svn-bin-dir.

<levels> and <spread> control the way the tested working copy is structured:
  <levels>: number of directory levels to create.
  <spread>: number of files and subdirectories created in each dir.


LIST WHAT IS ON RECORD

  benchmark.py list [ <branch>@<rev>,<levels>x<spread> ]

Find entries in the database for the given constraints. Any arguments can
be omitted. (To select only a rev, start with a '@', like '@123'; to select
only spread, start with an 'x', like "x100".)

Omit all args to get a listing of all available distinct entries.


COMPARE TIMINGS

  benchmark.py compare B@R,LxS B@R,LxS

Compare two kinds of timings (in text mode). Each B@R,LxS selects
timings from branch, revision, WC-levels and -spread by the same labels as
previously given for a 'run' call. Any elements can be omitted. For example:
  benchmark.py compare 1.7.0 trunk@1349903
    Compare the total timings of all combined '1.7.0' branch runs to
    all combined runs of 'trunk'-at-revision-1349903.
  benchmark.py compare 1.7.0,5x5 trunk@1349903,5x5
    Same as above, but only compare the working copy types with 5 levels
    and a spread of 5.


SHOW TIMINGS

  benchmark.py show <branch>@<rev>,<levels>x<spread>

Print out a summary of the timings selected from the given constraints.
Any arguments can be omitted (like for the 'list' command).


GENERATE CHARTS

  benchmark.py chart compare B@R,LxS B@R,LxS [ B@R,LxS ... ]

Produce a bar chart that compares any number of sets of timings. Timing sets
are supplied by B@R,LxS arguments (i.e. <branch>@<rev>,<levels>x<spread> as
provided for a 'run' call), where any number of elements may be omitted. The
less constraints you supply, the more timings are included (try it out with
the 'list' command). The first set is taken as a reference point for 100% and
+0 seconds. Each following dataset produces a set of labeled bar charts.
So, at least two constraint arguments must be provided.

Use the -c option to limit charts to specific command names.


EXAMPLES

# Run 3 benchmarks on svn 1.7.0. Timings are saved in benchmark.db.
# Provide label '1.7.0' and its Last-Changed-Rev for later reference.
# (You may also set your $PATH instead of using --svn-bin-dir.)
./benchmark.py run --svn-bin-dir ~/svn-prefix/1.7.0/bin 1.7.0@1181106,5x5 3

# Record 3 benchmark runs on trunk, again naming its Last-Changed-Rev.
./benchmark.py run --svn-bin-dir ~/svn-prefix/trunk/bin trunk@1352725,5x5 3

# Work with the results of above two runs
./benchmark.py list
./benchmark.py compare 1.7.0 trunk
./benchmark.py show 1.7.0 trunk
./benchmark.py chart compare 1.7.0 trunk
./benchmark.py chart compare 1.7.0 trunk -c "update,commit,TOTAL RUN"

# Rebuild r1352598, run it and chart improvements since 1.7.0.
svn up -r1352598 ~/src/trunk
make -C ~/src/trunk dist-clean install
export PATH="$HOME/svn-prefix/trunk/bin:$PATH"
which svn
./benchmark.py run trunk@1352598,5x5 3
./benchmark.py chart compare 1.7.0 trunk@1352598 trunk@1352725 -o chart.svg


GLOBAL OPTIONS"""

import os
import time
import datetime
import sqlite3
import optparse
import tempfile
import subprocess
import random
import shutil
import stat
import string

IGNORE_COMMANDS = ('--version', )
TOTAL_RUN = 'TOTAL RUN'

j = os.path.join

def time_str():
  return time.strftime('%Y-%m-%d %H:%M:%S');

def timedelta_to_seconds(td):
  return ( float(td.seconds)
           + float(td.microseconds) / (10**6)
           + td.days * 24 * 60 * 60 )

def run_cmd(cmd, stdin=None, shell=False, verbose=False):
  if options.verbose:
    if shell:
      printable_cmd = cmd
    else:
      printable_cmd = ' '.join(cmd)
    print 'CMD:', printable_cmd

  if stdin:
    stdin_arg = subprocess.PIPE
  else:
    stdin_arg = None

  p = subprocess.Popen(cmd,
                       stdin=stdin_arg,
                       stdout=subprocess.PIPE,
                       stderr=subprocess.PIPE,
                       shell=shell)
  stdout,stderr = p.communicate(input=stdin)

  if verbose:
    if (stdout):
      print "STDOUT: [[[\n%s]]]" % ''.join(stdout)
  if (stderr):
    print "STDERR: [[[\n%s]]]" % ''.join(stderr)

  return stdout, stderr


_next_unique_basename_count = 0

def next_unique_basename(prefix):
  global _next_unique_basename_count
  _next_unique_basename_count += 1
  return '_'.join((prefix, str(_next_unique_basename_count)))


si_units = [
    (1000 ** 5, 'P'),
    (1000 ** 4, 'T'), 
    (1000 ** 3, 'G'), 
    (1000 ** 2, 'M'), 
    (1000 ** 1, 'K'),
    (1000 ** 0, ''),
    ]
def n_label(n):
    """(stolen from hurry.filesize)"""
    for factor, suffix in si_units:
        if n >= factor:
            break
    amount = int(n/factor)
    if isinstance(suffix, tuple):
        singular, multiple = suffix
        if amount == 1:
            suffix = singular
        else:
            suffix = multiple
    return str(amount) + suffix


def split_arg_once(l_r, sep):
  if not l_r:
    return (None, None)
  if sep in l_r:
    l, r = l_r.split(sep)
  else:
    l = l_r
    r = None
  if not l:
    l = None
  if not r:
    r = None
  return (l, r)

RUN_KIND_SEPARATORS=('@', ',', 'x')

class RunKind:
  def __init__(self, b_r_l_s):
    b_r, l_s = split_arg_once(b_r_l_s, RUN_KIND_SEPARATORS[1])
    self.branch, self.revision = split_arg_once(b_r, RUN_KIND_SEPARATORS[0])
    self.levels, self.spread = split_arg_once(l_s, RUN_KIND_SEPARATORS[2])
    if self.levels: self.levels = int(self.levels)
    if self.spread: self.spread = int(self.spread)

    label_parts = []
    if self.branch:
      label_parts.append(self.branch)
    if self.revision:
      label_parts.append(RUN_KIND_SEPARATORS[0])
      label_parts.append(self.revision)
    if self.levels or self.spread:
      label_parts.append(RUN_KIND_SEPARATORS[1])
      if self.levels:
        label_parts.append(str(self.levels))
      if self.spread:
        label_parts.append(RUN_KIND_SEPARATORS[2])
        label_parts.append(str(self.spread))
    self.label = ''.join(label_parts)

  def args(self):
    return (self.branch, self.revision, self.levels, self.spread)


PATHNAME_VALID_CHARS = "-_.,@%s%s" % (string.ascii_letters, string.digits)
def filesystem_safe_string(s):
  return ''.join(c for c in s if c in PATHNAME_VALID_CHARS)

def do_div(ref, val):
  if ref:
    return float(val) / float(ref)
  else:
    return 0.0

def do_diff(ref, val):
  return float(val) - float(ref)


# ------------------------- database -------------------------

class TimingsDb:
  def __init__(self, db_path):
    self.db_path = db_path;
    self.conn = sqlite3.connect(db_path)
    self.ensure_tables_created()

  def ensure_tables_created(self):
    c = self.conn.cursor()

    c.execute("""SELECT name FROM sqlite_master WHERE type='table' AND
              name='batch'""")
    if c.fetchone():
      # exists
      return

    print 'Creating database tables.'
    c.executescript('''
        CREATE TABLE batch (
          batch_id INTEGER PRIMARY KEY AUTOINCREMENT,
          started TEXT,
          ended TEXT
        );

        CREATE TABLE run_kind (
          run_kind_id INTEGER PRIMARY KEY AUTOINCREMENT,
          branch TEXT NOT NULL,
          revision TEXT NOT NULL,
          wc_levels INTEGER,
          wc_spread INTEGER,
          UNIQUE(branch, revision, wc_levels, wc_spread)
        );

        CREATE TABLE run (
          run_id INTEGER PRIMARY KEY AUTOINCREMENT,
          batch_id INTEGER NOT NULL REFERENCES batch(batch_id),
          run_kind_id INTEGER NOT NULL REFERENCES run_kind(run_kind_id),
          started TEXT,
          ended TEXT,
          aborted INTEGER
        );

        CREATE TABLE timings (
          run_id INTEGER NOT NULL REFERENCES run(run_id),
          command TEXT NOT NULL,
          sequence INTEGER,
          timing REAL
        );'''
      )
    self.conn.commit()
    c.close();


class Batch:
  def __init__(self, db):
    self.db = db
    self.started = time_str()
    c = db.conn.cursor()
    c.execute("INSERT INTO batch (started) values (?)", (self.started,))
    db.conn.commit()
    self.id = c.lastrowid
    c.close()

  def done(self):
    conn = self.db.conn
    c = conn.cursor()
    c.execute("""
        UPDATE batch
        SET ended = ?
        WHERE batch_id = ?""",
        (time_str(), self.id))
    conn.commit()
    c.close()

class Run:
  def __init__(self, batch, run_kind):
    self.batch = batch
    conn = self.batch.db.conn
    c = conn.cursor()

    c.execute("""
        SELECT run_kind_id FROM run_kind
        WHERE branch = ?
          AND revision = ?
          AND wc_levels = ?
          AND wc_spread = ?""",
        run_kind.args())
    kind_ids = c.fetchone()
    if kind_ids:
      kind_id = kind_ids[0]
    else:
      c.execute("""
          INSERT INTO run_kind (branch, revision, wc_levels, wc_spread)
          VALUES (?, ?, ?, ?)""",
          run_kind.args())
      conn.commit()
      kind_id = c.lastrowid

    self.started = time_str()
    
    c.execute("""
        INSERT INTO run
          (batch_id, run_kind_id, started)
        VALUES
          (?, ?, ?)""",
        (self.batch.id, kind_id, self.started))
    conn.commit()
    self.id = c.lastrowid
    c.close();
    self.tic_at = None
    self.current_command = None
    self.timings = []

  def tic(self, command):
    if command in IGNORE_COMMANDS:
      return
    self.toc()
    self.current_command = command
    self.tic_at = datetime.datetime.now()

  def toc(self):
    if self.current_command and self.tic_at:
      toc_at = datetime.datetime.now()
      self.remember_timing(self.current_command,
                         timedelta_to_seconds(toc_at - self.tic_at))
    self.current_command = None
    self.tic_at = None

  def remember_timing(self, command, seconds):
    self.timings.append((command, seconds))

  def submit_timings(self):
    conn = self.batch.db.conn
    c = conn.cursor()
    print 'submitting...'

    c.executemany("""
      INSERT INTO timings
        (run_id, command, sequence, timing)
      VALUES
        (?, ?, ?, ?)""",
      [(self.id, t[0], (i + 1), t[1]) for i,t in enumerate(self.timings)])

    conn.commit()
    c.close()

  def done(self, aborted=False):
    conn = self.batch.db.conn
    c = conn.cursor()
    c.execute("""
        UPDATE run
        SET ended = ?, aborted = ?
        WHERE run_id = ?""",
        (time_str(), aborted, self.id))
    conn.commit()
    c.close()


class TimingQuery:
  def __init__(self, db, run_kind):
    self.cursor = db.conn.cursor()
    self.constraints = []
    self.values = []
    self.timings = None
    self.FROM_WHERE = """
         FROM batch AS b,
              timings AS t,
              run AS r,
              run_kind as k
         WHERE
              t.run_id = r.run_id
              AND k.run_kind_id = r.run_kind_id
              AND b.batch_id = r.batch_id
              AND r.aborted = 0
         """
    self.append_constraint('k', 'branch', run_kind.branch)
    self.append_constraint('k', 'revision', run_kind.revision)
    self.append_constraint('k', 'wc_levels', run_kind.levels)
    self.append_constraint('k', 'wc_spread', run_kind.spread)
    self.label = run_kind.label

  def append_constraint(self, table, name, val):
    if val:
      self.constraints.append('AND %s.%s = ?' % (table, name))
      self.values.append(val)

  def remove_last_constraint(self):
    del self.constraints[-1]
    del self.values[-1]

  def get_sorted_X(self, x, n=1):
    query = ['SELECT DISTINCT %s' % x,
             self.FROM_WHERE ]
    query.extend(self.constraints)
    query.append('ORDER BY %s' % x)
    c = db.conn.cursor()
    try:
      #print ' '.join(query)
      c.execute(' '.join(query), self.values)
      if n == 1:
        return [tpl[0] for tpl in c.fetchall()]
      else:
        return c.fetchall()
    finally:
      c.close()

  def get_sorted_command_names(self):
    return self.get_sorted_X('t.command')

  def get_sorted_branches(self):
    return self.get_sorted_X('k.branch')

  def get_sorted_revisions(self):
    return self.get_sorted_X('k.revision')

  def get_sorted_levels_spread(self):
    return self.get_sorted_X('k.wc_levels,k.wc_spread', n = 2)

  def count_runs_batches(self):
    query = ["""SELECT
                  count(DISTINCT r.run_id),
                  count(DISTINCT b.batch_id)""",
             self.FROM_WHERE ]
    query.extend(self.constraints)
    c = db.conn.cursor()
    try:
      #print ' '.join(query)
      c.execute(' '.join(query), self.values)
      return c.fetchone()
    finally:
      c.close()

  def get_command_timings(self, command):
    query = ["""SELECT
                  count(t.timing),
                  min(t.timing),
                  max(t.timing),
                  avg(t.timing)""",
             self.FROM_WHERE ]
    self.append_constraint('t', 'command', command)
    try:
      query.extend(self.constraints)
      c = db.conn.cursor()
      try:
        c.execute(' '.join(query), self.values)
        return c.fetchone()
      finally:
        c.close()
    finally:
      self.remove_last_constraint()

  def get_timings(self):
    if self.timings:
      return self.timings
    self.timings = {}
    for command_name in self.get_sorted_command_names():
      self.timings[command_name] = self.get_command_timings(command_name)
    return self.timings
      

# ------------------------------------------------------------ run tests


def perform_run(batch, run_kind,
                svn_bin, svnadmin_bin, verbose):

  run = Run(batch, run_kind)

  def create_tree(in_dir, _levels, _spread):
    try:
      os.mkdir(in_dir)
    except:
      pass

    for i in range(_spread):
      # files
      fn = j(in_dir, next_unique_basename('file'))
      f = open(fn, 'w')
      f.write('This is %s\n' % fn)
      f.close()

      # dirs
      if (_levels > 1):
        dn = j(in_dir, next_unique_basename('dir'))
        create_tree(dn, _levels - 1, _spread)

  def svn(*args):
    name = args[0]

    cmd = [ svn_bin ]
    cmd.extend( list(args) )
    if verbose:
      print 'svn cmd:', ' '.join(cmd)

    stdin = None
    if stdin:
      stdin_arg = subprocess.PIPE
    else:
      stdin_arg = None

    run.tic(name)
    try:
      p = subprocess.Popen(cmd,
                           stdin=stdin_arg,
                           stdout=subprocess.PIPE,
                           stderr=subprocess.PIPE,
                           shell=False)
      stdout,stderr = p.communicate(input=stdin)
    except OSError:
      stdout = stderr = None
    finally:
      run.toc()

    if verbose:
      if (stdout):
        print "STDOUT: [[[\n%s]]]" % ''.join(stdout)
      if (stderr):
        print "STDERR: [[[\n%s]]]" % ''.join(stderr)

    return stdout,stderr


  def add(*args):
    return svn('add', *args)

  def ci(*args):
    return svn('commit', '-mm', *args)

  def up(*args):
    return svn('update', *args)

  def st(*args):
    return svn('status', *args)

  def info(*args):
    return svn('info', *args)

  _chars = [chr(x) for x in range(ord('a'), ord('z') +1)]

  def randstr(len=8):
    return ''.join( [random.choice(_chars) for i in range(len)] )

  def _copy(path):
    dest = next_unique_basename(path + '_copied')
    svn('copy', path, dest)

  def _move(path):
    dest = path + '_moved'
    svn('move', path, dest)

  def _propmod(path):
    so, se = svn('proplist', path)
    propnames = [line.strip() for line in so.strip().split('\n')[1:]]

    # modify?
    if len(propnames):
      svn('ps', propnames[len(propnames) / 2], randstr(), path)

    # del?
    if len(propnames) > 1:
      svn('propdel', propnames[len(propnames) / 2], path)

  def _propadd(path):
    # set a new one.
    svn('propset', randstr(), randstr(), path)

  def _mod(path):
    if os.path.isdir(path):
      _propmod(path)
      return

    f = open(path, 'a')
    f.write('\n%s\n' % randstr())
    f.close()

  def _add(path):
    if os.path.isfile(path):
      return _mod(path)

    if random.choice((True, False)):
      # create a dir
      svn('mkdir', j(path, next_unique_basename('new_dir')))
    else:
      # create a file
      new_path = j(path, next_unique_basename('new_file'))
      f = open(new_path, 'w')
      f.write(randstr())
      f.close()
      svn('add', new_path)

  def _del(path):
    svn('delete', path)

  _mod_funcs = (_mod, _add, _propmod, _propadd, )#_copy,) # _move, _del)

  def modify_tree(in_dir, fraction):
    child_names = os.listdir(in_dir)
    for child_name in child_names:
      if child_name[0] == '.':
        continue
      if random.random() < fraction:
        path = j(in_dir, child_name)
        random.choice(_mod_funcs)(path)

    for child_name in child_names:
      if child_name[0] == '.': continue
      path = j(in_dir, child_name)
      if os.path.isdir(path):
        modify_tree(path, fraction)

  def propadd_tree(in_dir, fraction):
    for child_name in os.listdir(in_dir):
      if child_name[0] == '.': continue
      path = j(in_dir, child_name)
      if random.random() < fraction:
        _propadd(path)
      if os.path.isdir(path):
        propadd_tree(path, fraction)


  def rmtree_onerror(func, path, exc_info):
    """Error handler for ``shutil.rmtree``.

    If the error is due to an access error (read only file)
    it attempts to add write permission and then retries.

    If the error is for another reason it re-raises the error.

    Usage : ``shutil.rmtree(path, onerror=onerror)``
    """
    if not os.access(path, os.W_OK):
      # Is the error an access error ?
      os.chmod(path, stat.S_IWUSR)
      func(path)
    else:
      raise

  base = tempfile.mkdtemp()

  # ensure identical modifications for every run
  random.seed(0)

  aborted = True

  try:
    repos = j(base, 'repos')
    repos = repos.replace('\\', '/')
    wc = j(base, 'wc')
    wc2 = j(base, 'wc2')

    if repos.startswith('/'):
      file_url = 'file://%s' % repos
    else:
      file_url = 'file:///%s' % repos

    print '\nRunning svn benchmark in', base
    print 'dir levels: %s; new files and dirs per leaf: %s' %(
          run_kind.levels, run_kind.spread)

    started = datetime.datetime.now()

    try:
      run_cmd([svnadmin_bin, 'create', repos])
      svn('checkout', file_url, wc)

      trunk = j(wc, 'trunk')
      create_tree(trunk, run_kind.levels, run_kind.spread)
      add(trunk)
      st(wc)
      ci(wc)
      up(wc)
      propadd_tree(trunk, 0.05)
      ci(wc)
      up(wc)
      st(wc)
      info('-R', wc)

      trunk_url = file_url + '/trunk'
      branch_url = file_url + '/branch'

      svn('copy', '-mm', trunk_url, branch_url)
      st(wc)

      up(wc)
      st(wc)
      info('-R', wc)

      svn('checkout', trunk_url, wc2)
      st(wc2)
      modify_tree(wc2, 0.5)
      st(wc2)
      ci(wc2)
      up(wc2)
      up(wc)

      svn('switch', branch_url, wc2)
      modify_tree(wc2, 0.5)
      st(wc2)
      info('-R', wc2)
      ci(wc2)
      up(wc2)
      up(wc)

      modify_tree(trunk, 0.5)
      st(wc)
      ci(wc)
      up(wc2)
      up(wc)

      svn('merge', '--accept=postpone', trunk_url, wc2)
      st(wc2)
      info('-R', wc2)
      svn('resolve', '--accept=mine-conflict', wc2)
      st(wc2)
      svn('resolved', '-R', wc2)
      st(wc2)
      info('-R', wc2)
      ci(wc2)
      up(wc2)
      up(wc)

      svn('merge', '--accept=postpone', '--reintegrate', branch_url, trunk)
      st(wc)
      svn('resolve', '--accept=mine-conflict', wc)
      st(wc)
      svn('resolved', '-R', wc)
      st(wc)
      ci(wc)
      up(wc2)
      up(wc)

      svn('delete', j(wc, 'branch'))
      ci(wc)
      up(wc)

      aborted = False

    finally:
      stopped = datetime.datetime.now()
      print '\nDone with svn benchmark in', (stopped - started)

      run.remember_timing(TOTAL_RUN,
                        timedelta_to_seconds(stopped - started))
  finally:
    run.done(aborted)
    run.submit_timings()
    shutil.rmtree(base, onerror=rmtree_onerror)

  return aborted


# ---------------------------------------------------------------------

    
def cmdline_run(db, options, run_kind_str, N=1):
  run_kind = RunKind(run_kind_str)
  N = int(N)

  print 'Hi, going to run a Subversion benchmark series of %d runs...' % N
  print 'Label is %s' % run_kind.label

  # can we run the svn binaries?
  svn_bin = j(options.svn_bin_dir, 'svn')
  svnadmin_bin = j(options.svn_bin_dir, 'svnadmin')

  for b in (svn_bin, svnadmin_bin):
    so,se = run_cmd([b, '--version'])
    if not so:
      print "Can't run", b
      exit(1)

    print ', '.join([s.strip() for s in so.split('\n')[:2]])

  batch = Batch(db)

  for i in range(N):
    print 'Run %d of %d' % (i + 1, N)
    perform_run(batch, run_kind,
                svn_bin, svnadmin_bin, options.verbose)

  batch.done()


def cmdline_list(db, options, run_kind_str=None):
  run_kind = RunKind(run_kind_str)

  constraints = []
  def add_if_not_none(name, val):
    if val:
      constraints.append('  %s = %s' % (name, val))
  add_if_not_none('branch', run_kind.branch)
  add_if_not_none('revision', run_kind.revision)
  add_if_not_none('levels', run_kind.levels)
  add_if_not_none('spread', run_kind.spread)
  if constraints:
    print 'For\n', '\n'.join(constraints)
  print 'I found:'

  d = TimingQuery(db, run_kind)
  
  cmd_names = d.get_sorted_command_names()
  if cmd_names:
    print '\n%d command names:\n ' % len(cmd_names), '\n  '.join(cmd_names)

  branches = d.get_sorted_branches()
  if branches and (len(branches) > 1 or branches[0] != run_kind.branch):
    print '\n%d branches:\n ' % len(branches), '\n  '.join(branches)

  revisions = d.get_sorted_revisions()
  if revisions and (len(revisions) > 1 or revisions[0] != run_kind.revision):
    print '\n%d revisions:\n ' % len(revisions), '\n  '.join(revisions)

  levels_spread = d.get_sorted_levels_spread()
  if levels_spread and (
       len(levels_spread) > 1
       or levels_spread[0] != (run_kind.levels, run_kind.spread)):
    print '\n%d kinds of levels x spread:\n ' % len(levels_spread), '\n  '.join(
            [ ('%dx%d' % (l, s)) for l,s in levels_spread ])

  print "\n%d runs in %d batches.\n" % (d.count_runs_batches())


def cmdline_show(db, options, *run_kind_strings):
  for run_kind_str in run_kind_strings:
    run_kind = RunKind(run_kind_str)

    q = TimingQuery(db, run_kind)
    timings = q.get_timings()

    s = []
    s.append('Timings for %s' % run_kind.label)
    s.append('   N    min     max     avg   operation  (unit is seconds)')

    for command_name in q.get_sorted_command_names():
      if options.command_names and command_name not in options.command_names:
        continue
      n, tmin, tmax, tavg = timings[command_name]

      s.append('%4s %7.2f %7.2f %7.2f  %s' % (
                 n_label(n),
                 tmin,
                 tmax,
                 tavg,
                 command_name))

    print '\n'.join(s)


def cmdline_compare(db, options, left_str, right_str):
  left_kind = RunKind(left_str)
  right_kind = RunKind(right_str)

  leftq = TimingQuery(db, left_kind)
  left = leftq.get_timings()
  if not left:
    print "No timings for", left_kind.label
    exit(1)

  rightq = TimingQuery(db, right_kind)
  right = rightq.get_timings()
  if not right:
    print "No timings for", right_kind.label
    exit(1)

  label = 'Compare %s to %s' % (right_kind.label, left_kind.label)

  s = [label]

  verbose = options.verbose
  if not verbose:
    s.append('       N        avg         operation')
  else:
    s.append('       N        min              max              avg         operation')

  command_names = [name for name in leftq.get_sorted_command_names()
                   if name in right]
  if options.command_names:
    command_names = [name for name in command_names
                     if name in options.command_names]

  for command_name in command_names:
    left_N, left_min, left_max, left_avg = left[command_name]
    right_N, right_min, right_max, right_avg = right[command_name]

    N_str = '%s/%s' % (n_label(left_N), n_label(right_N))
    avg_str = '%7.2f|%+7.3f' % (do_div(left_avg, right_avg),
                                do_diff(left_avg, right_avg))

    if not verbose:
      s.append('%9s %-16s  %s' % (N_str, avg_str, command_name))
    else:
      min_str = '%7.2f|%+7.3f' % (do_div(left_min, right_min),
                                  do_diff(left_min, right_min))
      max_str = '%7.2f|%+7.3f' % (do_div(left_max, right_max),
                                  do_diff(left_max, right_max))

      s.append('%9s %-16s %-16s %-16s  %s' % (N_str, min_str, max_str, avg_str,
                                          command_name))

  s.extend([
    '(legend: "1.23|+0.45" means: slower by factor 1.23 and by 0.45 seconds;',
    ' factor < 1 and seconds < 0 means \'%s\' is faster.'
    % right_kind.label,
    ' "2/3" means: \'%s\' has 2 timings on record, the other has 3.)'
    % left_kind.label
    ])


  print '\n'.join(s)


# ------------------------------------------------------- charts

def cmdline_chart_compare(db, options, *args):
  import matplotlib
  matplotlib.use('Agg')
  import numpy as np
  import matplotlib.pylab as plt

  labels = []
  timing_sets = []
  command_names = None

  for arg in args:
    run_kind = RunKind(arg)
    query = TimingQuery(db, run_kind)
    timings = query.get_timings()
    if not timings:
      print "No timings for", run_kind.label
      exit(1)
    labels.append(run_kind.label)
    timing_sets.append(timings)

    if command_names:
      command_names_len = len(command_names)
      i = 0
      while i < command_names_len:
        if not command_names[i] in timings:
          del command_names[i]
          command_names_len -= 1
        else:
          i += 1
    else:
      command_names = query.get_sorted_command_names()

  if options.command_names:
    command_names = [name for name in command_names
                     if name in options.command_names]

  chart_path = options.chart_path
  if not chart_path:
    chart_path = 'compare_' + '_'.join(
      [ filesystem_safe_string(l) for l in labels ]
      ) + '.svg'
                  
  print '\nwriting chart file:', chart_path

  N = len(command_names)
  M = len(timing_sets) - 1

  ind = np.arange(N)  # the x locations for the groups
  width = 1. / (1.2 + M)     # the width of the bars
  dist = 0.15

  fig = plt.figure(figsize=(0.33*N*M,12))
  plot1 = fig.add_subplot(211)
  plot2 = fig.add_subplot(212)

  # invisible lines that make sure the scale doesn't get minuscule
  plot1.axhline(y=101, color='white', linewidth=0.01)
  plot1.axhline(y=95.0, color='white', linewidth=0.01)
  plot2.axhline(y=0.1, color='white', linewidth=0.01)
  plot2.axhline(y=-0.5, color='white', linewidth=0.01)

  reference = timing_sets[0]

  ofs = 0

  for label_i in range(1, len(labels)):
    timings = timing_sets[label_i]
    divs = []
    diffs = []
    divs_color = []
    deviations = []
    for command_name in command_names:
      ref_N, ref_min, ref_max, ref_avg = reference[command_name]
      this_N, this_min, this_max, this_avg = timings[command_name]

      val = 100. * (do_div(ref_avg, this_avg) - 1.0)
      if val < 0:
        col = '#55dd55'
      else:
        col = '#dd5555'
      divs.append(val)
      divs_color.append(col)
      diffs.append( do_diff(ref_avg, this_avg) )
      deviations.append(this_max / this_min)

    rects = plot1.bar(ind + ofs, divs, width * (1.0 - dist),
                      color=divs_color, bottom=100.0, edgecolor='none')

    for i in range(len(rects)):
      x = rects[i].get_x() + width / 2.2
      div = divs[i]
      label = labels[label_i]

      plot1.text(x, 100.,
                 ' %+5.1f%% %s' % (div,label),
                 ha='center', va='top', size='small',
                 rotation=-90, family='monospace')

    rects = plot2.bar(ind + ofs, diffs, width * 0.9,
                   color=divs_color, bottom=0.0, edgecolor='none')

    for i in range(len(rects)):
      x = rects[i].get_x() + width / 2.2
      diff = diffs[i]
      label = labels[label_i]

      plot2.text(x, 0.,
                 ' %+5.2fs %s' % (diff,label),
                 ha='center', va='top', size='small',
                 rotation=-90, family='monospace')

    ofs += width

  plot1.set_title('Speed change compared to %s [%%]' % labels[0])
  plot1.set_xticks(ind + (width / 2.))
  plot1.set_xticklabels(command_names, rotation=-55,
                        horizontalalignment='left',
                        size='x-small', weight='bold')
  plot1.axhline(y=100.0, color='#555555', linewidth=0.2)
  plot2.set_title('[seconds]')
  plot2.set_xticks(ind + (width / 2.))
  plot2.set_xticklabels(command_names, rotation=-55,
                        horizontalalignment='left',
                        size='medium', weight='bold')
  plot2.axhline(y=0.0, color='#555555', linewidth=0.2)

  margin = 1.5/(N*M)
  fig.subplots_adjust(bottom=0.1, top=0.97,
                      left=margin,
                      right=1.0-(margin / 2.))

  #plot1.legend( (rects1[0], rects2[0]), (left_label, right_label) )

  #plt.show()
  plt.savefig(chart_path)

# ------------------------------------------------------------ main


# Custom option formatter, keeping newlines in the description.
# adapted from:
# http://groups.google.com/group/comp.lang.python/msg/09f28e26af0699b1
import textwrap
class IndentedHelpFormatterWithNL(optparse.IndentedHelpFormatter):
  def format_description(self, description):
    if not description: return ""
    desc_width = self.width - self.current_indent
    indent = " "*self.current_indent
    bits = description.split('\n')
    formatted_bits = [
      textwrap.fill(bit,
        desc_width,
        initial_indent=indent,
        subsequent_indent=indent)
      for bit in bits]
    result = "\n".join(formatted_bits) + "\n"
    return result 

if __name__ == '__main__':
  parser = optparse.OptionParser(formatter=IndentedHelpFormatterWithNL())
  # -h is automatically added.
  ### should probably expand the help for that. and see about -?
  parser.add_option('-v', '--verbose', action='store_true', dest='verbose',
                    help='Verbose operation')
  parser.add_option('-b', '--svn-bin-dir', action='store', dest='svn_bin_dir',
                    default='',
                    help='Specify directory to find Subversion binaries in')
  parser.add_option('-f', '--db-path', action='store', dest='db_path',
                    default='benchmark.db',
                    help='Specify path to SQLite database file')
  parser.add_option('-o', '--chart-path', action='store', dest='chart_path',
                    help='Supply a path for chart output.')
  parser.add_option('-c', '--command-names', action='store',
                    dest='command_names',
                    help='Comma separated list of command names to limit to.')

  parser.set_description(__doc__)
  parser.set_usage('')


  options, args = parser.parse_args()

  def usage(msg=None):
    parser.print_help()
    if msg:
      print
      print msg
    exit(1)

  # there should be at least one arg left: the sub-command
  if not args:
    usage('No command argument supplied.')

  cmd = args[0]
  del args[0]

  db = TimingsDb(options.db_path)

  if cmd == 'run':
    if len(args) < 1 or len(args) > 2:
      usage()
    cmdline_run(db, options, *args)

  elif cmd == 'compare':
    if len(args) < 2:
      usage()
    cmdline_compare(db, options, *args)

  elif cmd == 'list':
    cmdline_list(db, options, *args)

  elif cmd == 'show':
    cmdline_show(db, options, *args)

  elif cmd == 'chart':
    if 'compare'.startswith(args[0]):
      cmdline_chart_compare(db, options, *args[1:])
    else:
      usage()

  else:
    usage('Unknown command argument: %s' % cmd)
