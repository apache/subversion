#!/usr/bin/env python

import os
import sys
import getopt
import popen2
import string
import shutil


CVS_CMD = 'cvs'
SVN_CMD = 'svn'


def usage():
  print 'USAGE: %s cvs-repos-path svn-repos-path' \
        % os.path.basename(sys.argv[0])
  print '  --help, -h       print this usage message and exit with success'


def error(msg):
  sys.stderr.write('Error: ' + str(msg) + '\n')


class CvsRepos:
  def __init__(self, path):
    path = os.path.abspath(path)
    if not os.path.isdir(path):
      raise RuntimeError('CVS path is not a directory')
    if os.path.exists(os.path.join(path, 'CVSROOT')):
      # A whole CVS repository was selected
      self.cvsroot = path
      self.modules = []
      for entry in os.listdir(path):
        if entry != 'CVSROOT' and os.path.isdir(os.path.join(path, entry)):
          self.modules.append(( entry, os.path.basename(entry) ))
    else:
      # A sub-directory of a CVS repository was selected
      self.cvsroot = os.path.dirname(path)
      module = os.path.basename(path)
      while not os.path.exists(os.path.join(self.cvsroot, 'CVSROOT')):
        parent = os.path.dirname(self.cvsroot)
        if parent == path:
          raise RuntimeError('Cannot find the CVSROOT directory')
        module = os.path.join(os.path.basename(self.cvsroot), module)
        self.cvsroot = parent
      self.modules = [ ( module, None ) ]

  def _export_single(self, base_cmd, module, dest_path):
    cmd = base_cmd + [ '-d', dest_path, module ]
    pipe = popen2.Popen4(cmd)
    output = pipe.fromchild.read()
    status = pipe.wait()
    if status or output:
      print 'CMD FAILED:', string.join(cmd, ' ')
      print 'Output:'
      sys.stdout.write(output)
      raise RuntimeError('CVS command failed!')

  def export(self, dest_path, rev=None):
    os.mkdir(dest_path)
    base_cmd = [ CVS_CMD, '-Q', '-d', self.cvsroot, 'export' ]
    if rev:
      base_cmd.extend([ '-r', rev ])
    else:
      base_cmd.extend([ '-D', 'now' ])
    for module, subdir in self.modules:
      if subdir:
        this_dest_path = os.path.join(dest_path, subdir)
      else:
        this_dest_path = dest_path
      self._export_single(base_cmd, module, this_dest_path)


class SvnRepos:
  def __init__(self, url):
    self.url = url

  def export(self, url, dest_path):
    cmd = [ SVN_CMD, 'export', '-q', url, dest_path ]
    pipe = popen2.Popen4(cmd)
    output = pipe.fromchild.read()
    status = pipe.wait()
    if status or output:
      print 'CMD FAILED:', string.join(cmd, ' ')
      print 'Output:'
      sys.stdout.write(output)
      raise RuntimeError('SVN command failed!')

  def export_trunk(self, rel_url):
    self.export(self.url + '/trunk', rel_url)

  def export_tag(self, rel_url, tag):
    self.export(self.url + '/tags/' + tag, rel_url)

  def export_branch(self, rel_url, branch):
    self.export(self.url + '/branches/' + branch, rel_url)

  def list(self, rel_url):
    cmd = [ SVN_CMD, 'ls', self.url + '/' + rel_url ]
    pipe = popen2.Popen4(cmd)
    lines = pipe.fromchild.readlines()
    status = pipe.wait()
    if status:
      print 'CMD FAILED:', string.join(cmd, ' ')
      print 'Output:'
      sys.stdout.writelines(lines)
      raise RuntimeError('SVN command failed!')
    entries = []
    for line in lines:
      entries.append(line[:-2])
    return entries

  def tags(self):
    return self.list('tags')

  def branches(self):
    return self.list('branches')


def file_compare(base1, base2, rel_path):
  path1 = os.path.join(base1, rel_path)
  path2 = os.path.join(base2, rel_path)
  if open(path1, 'rb').read() != open(path2, 'rb').read():
    print '    ANOMALY: File contents differ for %s' % rel_path
    return 0
  return 1


def tree_compare(base1, base2, rel_path=''):
  if not rel_path:
    path1 = base1
    path2 = base2
  else:
    path1 = os.path.join(base1, rel_path)
    path2 = os.path.join(base2, rel_path)
  if os.path.isfile(path1) and os.path.isfile(path2):
    return file_compare(base1, base2, rel_path)
  if not os.path.isdir(path1) or not os.path.isdir(path2):
    print '    ANOMALY: Path type differ for %s' % rel_path
    return 0
  entries1 = os.listdir(path1)
  entries1.sort()
  entries2 = os.listdir(path2)
  entries2.sort()
  if entries1 != entries2:
    print '    ANOMALY: Directory contents differ for %s' % rel_path
    return 0
  ok = 1
  for entry in entries1:
    new_rel_path = os.path.join(rel_path, entry)
    if not tree_compare(base1, base2, new_rel_path):
      ok = 0
  return ok


def verify_contents(cvsroot, svn_url, tmpdir=''):
  cvs_export_dir = os.path.join(tmpdir, 'cvs-export')
  svn_export_dir = os.path.join(tmpdir, 'svn-export')

  cr = CvsRepos(cvsroot)
  sr = SvnRepos(svn_url)

  anomalies = []

  print 'Verifying trunk'
  cr.export(cvs_export_dir)
  sr.export_trunk(svn_export_dir)
  if not tree_compare(cvs_export_dir, svn_export_dir):
    anomalies.append('trunk')
  shutil.rmtree(cvs_export_dir)
  shutil.rmtree(svn_export_dir)

  for tag in sr.tags():
    print 'Verifying tag', tag
    cr.export(cvs_export_dir, tag)
    sr.export_tag(svn_export_dir, tag)
    if not tree_compare(cvs_export_dir, svn_export_dir):
      anomalies.append('tag:' + tag)
    shutil.rmtree(cvs_export_dir)
    shutil.rmtree(svn_export_dir)

  for branch in sr.branches():
    print 'Verifying branch', branch
    cr.export(cvs_export_dir, branch)
    sr.export_branch(svn_export_dir, branch)
    if not tree_compare(cvs_export_dir, svn_export_dir):
      anomalies.append('branch:' + branch)
    shutil.rmtree(cvs_export_dir)
    shutil.rmtree(svn_export_dir)

  print
  if len(anomalies):
    print len(anomalies), 'content anomalies detected:', anomalies
  else:
    print 'No content anomalies detected'


def verify(cvsroot, svn_url, tmpdir=''):
  return verify_contents(cvsroot, svn_url, tmpdir)


def main():
  try:
    opts, args = getopt.getopt(sys.argv[1:], 'h',
                               [ "help" ])
  except getopt.GetoptError, e:
    error(e)
    usage()
    sys.exit(1)

  for opt, value in opts:
    if (opt == '--help') or (opt == '-h'):
      usage()
      sys.exit(0)
      
  # Consistency check for options and arguments.
  if len(args) != 2:
    usage()
    sys.exit(1)

  cvsroot = args[0]
  svn_url = 'file://' + string.join([os.getcwd(), args[1]], '/')

  verify(cvsroot, svn_url)


if __name__ == '__main__':
  main()
