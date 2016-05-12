#!/usr/bin/env python
#
#  copy_repo.py: create multiple, interleaved copies of a set of repositories.
#
#  Subversion is a tool for revision control.
#  See http://subversion.apache.org for more information.
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
import os
import random
import shutil
import sys

class Separators:
  """ This class is a container for dummy / filler files.
      It will be used to create spaces between repository
      versions on disk, i.e. to simulate some aspect of
      real-world FS fragmentation.

      It gets initialized with some parent path as well as
      the desired average file size and will create a new
      such file with each call to write().  Automatic
      sharding keeps FS specific overhead at bay.  Call
      cleanup() to eventually delete all dummy files. """

  buffer = "A" * 4096
     """ Write this non-NULL contents into the dummy files. """

  def __init__(self, path, average_size):
    """ Initialize and store all dummy files in a '__tmp'
        sub-folder of PATH.  The size of each dummy file
        is a random value and will be slightly AVERAGE_SIZE
        kBytes on average.  A value of 0 will effectively
        disable dummy file creation. """

    self.path = os.path.join(path, '__tmp')
    self.size = average_size
    self.count = 0

    if os.path.exists(self.path):
      shutil.rmtree(self.path)

    os.mkdir(self.path)

  def write(self):
    """ Add a new dummy file """

    # Throw dice of a file size.
    # Factor 1024 for kBytes, factor 2 for being an average.
    size = (int)(float(self.size) * random.random() * 2 * 1024.0)

    # Don't create empty files.  This also implements the
    # "average = 0 means no files" rule.
    if size > 0:
      self.count += 1

      # Create a new shard for every 1000 files
      subfolder = os.path.join(self.path, str(self.count / 1000))
      if not os.path.exists(subfolder):
        os.mkdir(subfolder)

      # Create and write the file in 4k chunks.
      # Writing full chunks will result in average file sizes
      # being slightly above the SELF.SIZE.  That's good enough
      # for our purposes.
      f = open(os.path.join(subfolder, str(self.count)), "wb")
      while size > 0:
        f.write(self.buffer)
        size -= len(self.buffer)

      f.close()

  def cleanup(self):
    """ Get rid of all the files (and folders) that we created. """

    shutil.rmtree(self.path)

class Repository:
  """ Encapsulates key information of a repository.  Is is being
      used for copy sources only and contains information about
      its NAME, PATH, SHARD_SIZE, HEAD revision and MIN_UNPACKED_REV. """

  def _read_config(self, filename):
    """ Read and return all lines from FILENAME.
        This will be used to read 'format', 'current' etc. . """

    f = open(os.path.join(self.path, 'db', filename), "rb")
    lines = f.readlines()
    f.close()

    return lines

  def __init__(self, parent, name):
    """ Constructor collecting everything we need to know about
        the repository NAME within PARENT folder. """

    self.name = name
    self.path = os.path.join(parent, name)

    self.shard_size = int(self._read_config('format')[1].split(' ')[2])
    self.min_unpacked_rev = int(self._read_config('min-unpacked-rev')[0])
    self.head = int(self._read_config('current')[0])

  def needs_copy(self, revision):
    """ Return True if REVISION is a revision in this repository
        and is "directly copyable", i.e. is either non-packed or
        the first rev in a packed shard.  Everything else is either
        not a valid rev or already gets / got copied as part of
        some packed shard. """

    if revision > self.head:
      return False
    if revision < self.min_unpacked_rev:
      return revision % self.shard_size == 0

    return True

  @classmethod
  def is_repository(cls, path):
    """ Quick check that PATH is (probably) a repository.
        This is mainly to filter out aux files put next to
        (not inside) the repositories to copy. """

    format_path = os.path.join(path, 'db', 'format')
    return os.path.isfile(format_path)

class Multicopy:
  """ Helper class doing the actual copying.  It copies individual
      revisions and packed shards from the one source repository
      to multiple copies of it.  The copies have the same name
      as the source repo but with numbers 0 .. N-1 appended to it.

      The copy process is being initiated by the constructor
      (copies the repo skeleton w/o revision contents).  Revision
      contents is then copied by successive calls to the copy()
      method. """

  def _init_copy(self, number):
    """ Called from the constructor, this will copy SELF.SOURCE_REPO
        into NUMBER new repos below SELF.DEST_BASE but omit everything
        below db/revs and db/revprops. """

    src = self.source_repo.path
    dst = self.dest_base + str(number)

    # Copy the repo skeleton w/o revs and revprops
    shutil.copytree(src, dst, ignore=shutil.ignore_patterns('revs', 'revprops'))

    # Add revs and revprops
    self.dst_revs.append(os.path.join(dst, 'db', 'revs'))
    self.dst_revprops.append(os.path.join(dst, 'db', 'revprops'))

    os.mkdir(self.dst_revs[number])
    os.mkdir(self.dst_revprops[number])

  def _copy_packed_shard(self, shard, number):
    """ Copy packed shard number SHARD from SELF.SOURCE_REPO to
        the copy NUMBER below SELF.DEST_BASE. """

    # Shards are simple subtrees
    src_revs = os.path.join(self.src_revs, str(shard) + '.pack')
    dst_revs = os.path.join(self.dst_revs[number], str(shard) + '.pack')
    src_revprops = os.path.join(self.src_revprops, str(shard) + '.pack')
    dst_revprops = os.path.join(self.dst_revprops[number], str(shard) + '.pack')

    shutil.copytree(src_revs, dst_revs)
    shutil.copytree(src_revprops, dst_revprops)

    # Special case: revprops of rev 0 are never packed => extra copy
    if shard == 0:
      src_revprops = os.path.join(self.src_revprops, '0')
      dest_revprops = os.path.join(self.dst_revprops[number], '0')

      shutil.copytree(src_revprops, dest_revprops)

  def _copy_single_revision(self, revision, number):
    """ Copy non-packed REVISION from SELF.SOURCE_REPO to the copy
        NUMBER below SELF.DEST_BASE. """

    shard = str(revision / self.source_repo.shard_size)

    # Auto-create shard folder
    if revision % self.source_repo.shard_size == 0:
      os.mkdir(os.path.join(self.dst_revs[number], shard))
      os.mkdir(os.path.join(self.dst_revprops[number], shard))

    # Copy the rev file and the revprop file
    src_rev = os.path.join(self.src_revs, shard, str(revision))
    dest_rev = os.path.join(self.dst_revs[number], shard, str(revision))
    src_revprop = os.path.join(self.src_revprops, shard, str(revision))
    dest_revprop = os.path.join(self.dst_revprops[number], shard, str(revision))

    shutil.copyfile(src_rev, dest_rev)
    shutil.copyfile(src_revprop, dest_revprop)

  def __init__(self, source, target_parent, count):
    """ Initiate the copy process for the SOURCE repository to
        be copied COUNT times into the TARGET_PARENT directory. """

    self.source_repo = source
    self.dest_base = os.path.join(target_parent, source.name)

    self.src_revs = os.path.join(source.path, 'db', 'revs')
    self.src_revprops = os.path.join(source.path, 'db', 'revprops')

    self.dst_revs = []
    self.dst_revprops = []
    for i in range(0, count):
      self._init_copy(i)

  def copy(self, revision, number):
    """ Copy (packed or non-packed) REVISION from SELF.SOURCE_REPO
        to the copy NUMBER below SELF.DEST_BASE.

        SELF.SOURCE_REPO.needs_copy(REVISION) must be True. """

    if revision < self.source_repo.min_unpacked_rev:
      self._copy_packed_shard(revision / self.source_repo.shard_size, number)
    else:
      self._copy_single_revision(revision, number)

def copy_repos(src, dst, count, separator_size):
  """ Under DST, create COUNT copies of all repositories immediately
      below SRC.

      All copies will "interleaved" such that we copy each individual
      revision / packed shard to all target repos first before
      continuing with the next revision / packed shard.  After each
      round (revision / packed shard) insert a temporary file of
      SEPARATOR_SIZE kBytes on average to add more spacing between
      revisions.  The temp files get automatically removed at the end.

      Please note that this function will clear DST before copying
      anything into it. """

  # Remove any remnants from the target folder.
  # (DST gets auto-created by the first repo copy.)
  shutil.rmtree(dst)

  # Repositories to copy and the respective copy utilities
  repositories = []
  copies = []

  # Find repositories, initiate copies and determine the range of
  # revisions to copy in total
  max_revision = 0
  for name in os.listdir(src):
    if Repository.is_repository(os.path.join(src, name)):
      repository = Repository(src, name)
      repositories.append(repository)
      copies.append(Multicopy(repository, dst, count))

      if repository.head > max_revision:
        max_revision = repository.head

  # Temp file collection (spacers)
  separators = Separators(dst, separator_size)

  # Copy all repos in revision,number-major order
  for revision in xrange(0, max_revision + 1):
    for number in xrange(0, count):

      any_copy = False
      for i in xrange(0, len(repositories)):
        if repositories[i].needs_copy(revision):
          any_copy = True
          copies[i].copy(revision, number)

      # Don't add spacers when nothing got copied (REVISION is
      # packed in all repositories).
      if any_copy:
        separators.write()

  # Now that all data is in position, remove the spacers
  separators.cleanup()

def show_usage():
  """ Write a simple CL docstring """

  print("Copies and duplicates repositories in a way that mimics larger deployments.")
  print("")
  print("Usage:")
  print("copy_repo.py SRC DST COUNT SEPARATOR_SIZE")
  print("")
  print("SRC            Immediate parent folder of all the repositories to copy.")
  print("DST            Folder to copy into; current contents will be lost.")
  print("COUNT          Number of copies to create of each source repository.")
  print("SEPARATOR_SIZE Additional spacing, in kBytes, between revisions.")

#main function
if len(argv) == 5:
  copy_repos(sys.argv[1], sys.argv[2], int(sys.argv[3]), int(sys.argv[4]))
else:
  show_usage()
