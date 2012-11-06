#!/usr/bin/env python
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
# ====================================================================

# TODO: if this was part of core subversion, we'd have all sorts of nifty
#       checks, and could use a lot of existing code.

import os
import re
import sys
import shutil
import sqlite3


def usage():
  print("""usage: %s WC_SRC TARGET

Detatch the working copy subdirectory given by WC_SRC to TARGET.  This is
equivalent to copying WC_SRC to TARGET, but it inserts a new set of Subversion
metadata into TARGET/.svn, making TARGET a proper independent working copy.
""" % sys.argv[0])
  sys.exit(1)


def find_wcroot(wcdir):
  wcroot = os.path.abspath(wcdir)
  old_wcroot = ''
  while wcroot != old_wcroot:
    if os.path.exists(os.path.join(wcroot, '.svn', 'wc.db')):
      return wcroot

    old_wcroot = wcroot
    wcroot = os.path.dirname(wcroot)

  return None


def  migrate_sqlite(wc_src, target, wcroot):
  src_conn = sqlite3.connect(os.path.join(wcroot, '.svn', 'wc.db'))
  dst_conn = sqlite3.connect(os.path.join(target, '.svn', 'wc.db'))

  local_relsrc = os.path.relpath(wc_src, wcroot)

  src_c = src_conn.cursor()
  dst_c = dst_conn.cursor()

  # We're only going to attempt this if there are no locks or work queue
  # items in the source database
  ### This could probably be tightened up, but for now this suffices
  src_c.execute('select count(*) from wc_lock')
  count = int(src_c.fetchone()[0])
  assert count == 0

  src_c.execute('select count(*) from work_queue')
  count = int(src_c.fetchone()[0])
  assert count == 0

  # Copy over the schema
  src_c.execute('pragma user_version')
  user_version = src_c.fetchone()[0]
  # We only know how to handle format 29 working copies
  assert user_version == 29
  ### For some reason, sqlite doesn't like to parameterize the pragma statement
  dst_c.execute('pragma user_version = %d' % user_version)

  src_c.execute('select name, sql from sqlite_master')
  for row in src_c:
    if not row[0].startswith('sqlite_'):
      dst_c.execute(row[1])

  # Insert wcroot row
  dst_c.execute('insert into wcroot (id, local_abspath) values (?, ?)',
                (1, None))

  # Copy repositories rows
  ### Perhaps prune the repositories based upon the new NODES set?
  src_c.execute('select * from repository')
  for row in src_c:
    dst_c.execute('insert into repository values (?, ?, ?)',
                  row)

  # Copy the root node
  src_c.execute('select * from nodes where local_relpath = ?',
                (local_relsrc,))
  row = list(src_c.fetchone())
  row[1] = ''
  row[3] = None
  dst_c.execute('''insert into nodes values
                  (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?,
                   ?, ?, ?, ?, ?, ?, ?, ?)''', row)

  # Copy children nodes rows
  src_c.execute('select * from nodes where local_relpath like ?',
                (local_relsrc + '/%', ))
  for row in src_c:
    row = list(row)
    row[1] = row[1][len(local_relsrc) + 1:]
    row[3] = row[3][len(local_relsrc) + 1:]
    dst_c.execute('''insert into nodes values
                  (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?,
                   ?, ?, ?, ?, ?, ?, ?, ?)''',
                  row)

  # Copy root actual_node
  src_c.execute('select * from actual_node where local_relpath = ?',
                (local_relsrc, ))
  row = src_c.fetchone()
  if row:
    row = list(row)
    row[1] = ''
    row[2] = None
    dst_c.execute('''insert into actual_node values
                     (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)''', row)

  src_c.execute('select * from actual_node where local_relpath like ?',
                (local_relsrc + '/%', ))
  for row in src_c:
    row = list(row)
    row[1] = row[1][len(local_relsrc) + 1:]
    row[2] = row[2][len(local_relsrc) + 1:]
    dst_c.execute('''insert into actual_node values
                     (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)''', row)

  # Hard to know which locks we care about, so just copy 'em all (there aren't
  # likely to be many)
  src_c.execute('select * from lock')
  for row in src_c:
    dst_c.execute('insert into locks values (?, ?, ?, ?, ?, ?)', row)

  # EXTERNALS
  src_c.execute('select * from externals where local_relpath = ?',
                (local_relsrc, ))
  row = src_c.fetchone()
  if row:
    row = list(row)
    row[1] = ''
    row[2] = None
    dst_c.execute('''insert into externals values
                     (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)''', row)

  src_c.execute('select * from externals where local_relpath like ?',
                (local_relsrc + '/%', ))
  for row in src_c:
    row = list(row)
    row[1] = row[1][len(local_relsrc) + 1:]
    row[2] = row[2][len(local_relsrc) + 1:]
    dst_c.execute('''insert into externals values
                     (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)''', row)

  dst_conn.commit()
  src_conn.close()
  dst_conn.close()


def migrate_pristines(wc_src, target, wcroot):
  src_conn = sqlite3.connect(os.path.join(wcroot, '.svn', 'wc.db'))
  dst_conn = sqlite3.connect(os.path.join(target, '.svn', 'wc.db'))

  src_c = src_conn.cursor()
  dst_c = dst_conn.cursor()

  regex = re.compile('\$((?:md5 *)|(?:sha1))\$(.*)')
  src_proot = os.path.join(wcroot, '.svn', 'pristine')
  target_proot = os.path.join(target, '.svn', 'pristine')

  checksums = {}

  # Grab anything which needs a pristine
  src_c.execute('''select checksum from nodes
                   union
                   select older_checksum from actual_node
                   union
                   select left_checksum from actual_node
                   union
                   select right_checksum from actual_node''')
  for row in src_c:
    if row[0]:
      match = regex.match(row[0])
      assert match

      pristine = match.group(2)
      if pristine in checksums:
        checksums[pristine] += 1
      else:
        checksums[pristine] = 1

  for pristine, count in checksums.items():
    # Copy the pristines themselves over
    pdir = os.path.join(target_proot, pristine[0:2])
    if not os.path.exists(pdir):
      os.mkdir(pdir)
    path = os.path.join(pristine[0:2], pristine + '.svn-base')
    if os.path.exists(os.path.join(target_proot, path)):
      dst_c.execute
    else:
      shutil.copy2(os.path.join(src_proot, path),
                   os.path.join(target_proot, path))

    src_c.execute('select size, md5_checksum from pristine where checksum=?',
                  ('$sha1$' + pristine, ) )
    (size, md5) = src_c.fetchone()

    # Insert a db row for the pristine
    dst_c.execute('insert into pristine values (?, NULL, ?, ?, ?)',
                  ('$sha1$' + pristine, size, count, md5))

  dst_conn.commit()
  src_conn.close()
  dst_conn.close()


def migrate_metadata(wc_src, target, wcroot):
  # Make paths
  os.mkdir(os.path.join(target, '.svn'))
  os.mkdir(os.path.join(target, '.svn', 'tmp'))
  os.mkdir(os.path.join(target, '.svn', 'pristine'))
  open(os.path.join(target, '.svn', 'format'), 'w').write('12')
  open(os.path.join(target, '.svn', 'entries'), 'w').write('12')

  # Two major bits: sqlite data and pristines
  migrate_sqlite(wc_src, os.path.abspath(target), wcroot)
  migrate_pristines(wc_src, target, wcroot)


def main():
  if len(sys.argv) < 3:
    usage()

  wc_src = os.path.normpath(sys.argv[1])
  if not os.path.isdir(wc_src):
    print("%s does not exist or is not a directory" % wc_src)
    sys.exit(1)

  target = os.path.normpath(sys.argv[2])
  if os.path.exists(target):
    print("Target '%s' already exists" % target)
    sys.exit(1)

  wcroot = find_wcroot(wc_src)
  if not wcroot:
    print("'%s' is not part of a working copy" % wc_src)
    sys.exit(1)

  # Use the OS to copy the subdirectory over to the target
  shutil.copytree(wc_src, target)

  # Now migrate the worky copy data
  migrate_metadata(wc_src, target, wcroot)


if __name__ == '__main__':
  raise Exception("""This script is unfinished and not ready to be used on live data.
    Trust us.""")
  main()
