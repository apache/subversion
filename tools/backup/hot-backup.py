#!/usr/bin/env python
#
#  hot-backup.py: perform a "hot" backup of a Berkeley DB repository.
#                 (and clean old logfiles after backup completes.)
#
#  Subversion is a tool for revision control. 
#  See http://subversion.tigris.org for more information.
#    
# ====================================================================
# Copyright (c) 2000-2003 CollabNet.  All rights reserved.
#
# This software is licensed as described in the file COPYING, which
# you should have received as part of this distribution.  The terms
# are also available at http://subversion.tigris.org/license-1.html.
# If newer versions of this license are posted there, you may use a
# newer version instead, at your option.
#
# This software consists of voluntary contributions made by many
# individuals.  For exact contribution history, see the revision
# history and logs, available at http://subversion.tigris.org/.
# ====================================================================

######################################################################

import sys, os, shutil, string

######################################################################
# Global Settings

# Path to svnlook utility
svnlook = "/usr/local/bin/svnlook"

# Path to db_archive program
db_archive = "/usr/local/BerkeleyDB.4.0/bin/db_archive"

# Path to db_recover progrem
db_recover = "/usr/local/BerkeleyDB.4.0/bin/db_recover"

# Number of backups to keep around
num_backups = 64

######################################################################
# Command line arguments


if len(sys.argv) != 3:
  print "Usage: ", os.path.basename(sys.argv[0]), " <repos_path> <backup_path>"
  sys.exit(1)

# Path to repository
repo_dir = sys.argv[1]
repo = os.path.basename(repo_dir)

# Where to store the repository backup.  The backup will be placed in
# a *subdirectory* of this location, named after the youngest
# revision.
backup_dir = sys.argv[2]

######################################################################

print "Beginning hot backup of '"+ repo_dir + "'."


# Step 1:  get the youngest revision.

infile, outfile, errfile = os.popen3(svnlook + " youngest " + repo_dir)
stdout_lines = outfile.readlines()
stderr_lines = errfile.readlines()
outfile.close()
infile.close()
errfile.close()

youngest = string.strip(stdout_lines[0])
print "Youngest revision is", youngest


# Step 2:  copy the whole repository structure.

backup_subdir = os.path.join(backup_dir, repo + "-" + youngest)
print "Backing up repository to '" + backup_subdir + "'..."
shutil.copytree(repo_dir, backup_subdir)
print "Done."


# Step 3:  re-copy the logfiles.  They must *always* be copied last.

infile, outfile, errfile = os.popen3(db_archive + " -l -h "
                                     + os.path.join(repo_dir, "db"))
stdout_lines = outfile.readlines()
stderr_lines = errfile.readlines()
outfile.close()
infile.close()
errfile.close()

print "Re-copying logfiles:"

for item in stdout_lines:
  logfile = string.strip(item)
  src = os.path.join(repo_dir, "db", logfile)
  dst = os.path.join(backup_subdir, "db", logfile)
  print "   Re-copying logfile '" + logfile + "'..."
  shutil.copy(src, dst)
  
print "Backup completed."


# Step 4:  look for a write `lock' file in backup_dir, else make one.

lockpath = os.path.join(backup_dir, 'lock')
if os.path.exists(lockpath):
  print "Cannot cleanup logs:  lockfile already exists in", backup_dir
  sys.exit(0)

print "Writing lock for logfile cleanup..."
fp = open(lockpath, 'a')  # open in (a)ppend mode
fp.write("cleaning logfiles for repository " + repo_dir)
fp.close()

# Step 5:  put the archived database in a consistent state and remove
#          the shared-memory environment files.

infile, outfile, errfile = os.popen3(db_recover + " -h "
                                     + os.path.join(backup_subdir, "db"))
stdout_lines = outfile.readlines()
stderr_lines = errfile.readlines()
outfile.close()
infile.close()
errfile.close()

print "Running db_recover on the archived database:"
map(sys.stdout.write, stdout_lines)
map(sys.stdout.write, stderr_lines)

print "Done."

# Step 6:  ask db_archive which logfiles can be expunged, and remove them.

infile, outfile, errfile = os.popen3(db_archive + " -a -h "
                                     + os.path.join(repo_dir, "db"))
stdout_lines = outfile.readlines()
stderr_lines = errfile.readlines()
outfile.close()
infile.close()
errfile.close()

print "Cleaning obsolete logfiles:"

for item in stdout_lines:
  logfile = string.strip(item)
  print "   Deleting '", logfile, "'..."
  os.unlink(logfile)

print "Done."

# Step 7:  remove the write lock.

os.unlink(lockpath)
print "Lock removed.  Cleanup complete."

# Step 8:  finally, remove the repository back that's NUM_BACKUPS older
# than the one we just created.

kill_rev = int(youngest) - num_backups
old_backup_subdir = os.path.join(backup_dir, repo + "-" + `kill_rev`)
if os.path.exists(old_backup_subdir):
  print "Removing old backup: " + old_backup_subdir
  shutil.rmtree(old_backup_subdir)
