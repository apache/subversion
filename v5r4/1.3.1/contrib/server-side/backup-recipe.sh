#!/bin/sh

###########################################################################
#                                                                         #
#  This shell script demonstrates a backup/restore recipe for live        #
#  Subversion repositories, using a standard full+incrementals process.   #
#                                                                         #
#  This script is intended only as an example; the idea is that you       #
#  can read over it, understand how it works (it's extensively commented) #
#  and then implement real backup and restore scripts based on this       #
#  recipe.                                                                #
#                                                                         #
#  To reiterate: this is *not* a backup and restore solution.  It's       #
#  really just documentation, in the form of code with comments.          #
#                                                                         #
#  If you do implement your own scripts based on the recipe here, and     #
#  your implementations are generic enough to be generally useful,        #
#  please post them to dev@subversion.tigris.org.  It would be great if   #
#  we could offer a real solution, and not just a description of one.     #
#                                                                         #
#  This recipe is distilled from the Berkeley DB documentation, see       #
#  http://www.sleepycat.com/docs/ref/transapp/archival.html.              #
#                                                                         #
#  See also http://www.sleepycat.com/docs/ref/transapp/reclimit.html for  #
#  for possible problems using standard 'cp' in this recipe.              #
#                                                                         #
###########################################################################

# High-level overview of the full backup recipe:
# 
#    1. Ask BDB's db_archive for a list of unused log files.
#
#    2. Copy the entire db/ dir to the backup area.
#
#    3. Recopy all the logfiles to the backup area.  There may be more
#       logfiles now than there were when step (1) ran.
#
#    4. Remove the logfiles listed as inactive in step (1) from the
#       repository, though not from the backup.
#    
# High-level overview of the incremental backup recipe:
#
#    1. Just copy the Berkeley logfiles to a backup area.
#    
# High-level overview of the restoration recipe:
#
#    1. Copy all the datafiles and logfiles back to the repository, in
#       the same order they were backed up.
#
#    2. Run Berkeley's "catastrophic recovery" command on the repository.
#
# That's it.  Here we go...

# You might need to customize some of these paths.
SVN=svn
SVNADMIN=svnadmin
SVNLOOK=svnlook
# See http://www.sleepycat.com/docs/utility/db_archive.html:
DB_ARCHIVE=/usr/local/BerkeleyDB.4.2/bin/db_archive
# See http://www.sleepycat.com/docs/utility/db_recover.html:
DB_RECOVER=/usr/local/BerkeleyDB.4.2/bin/db_recover

# This is just source data to generate repository activity.
# Any binary file of about 64k will do, it doesn't have to be /bin/ls.
DATA_BLOB=/bin/ls

# You shouldn't need to customize below here.
SANDBOX=`pwd`/backups-test-tmp
FULL_BACKUPS=${SANDBOX}/full
INCREMENTAL_PREFIX=${SANDBOX}/incremental-logs
RECORDS=${SANDBOX}/records
PROJ=myproj
REPOS=${PROJ}-repos

rm -rf ${SANDBOX}
mkdir ${SANDBOX}
mkdir ${RECORDS}

cd ${SANDBOX}

${SVNADMIN} create --bdb-log-keep ${REPOS}
${SVN} co file://${SANDBOX}/${REPOS} wc

cd wc

# Put in enough data for us to exercise the logfiles.
cp ${DATA_BLOB} ./a1
cp ${DATA_BLOB} ./b1
cp ${DATA_BLOB} ./c1
${SVN} -q add a1 b1 c1
${SVN} -q ci -m "Initial add."

echo "Created test data."

cd ..

# Exercise the logfiles by moving data around a lot.  Note that we
# avoid adds-with-history, since those cause much less Berkeley
# activity than plain adds.
#
# Call this from the parent of wc, that is, with $SANDBOX as CWD.
# Pass one argument, a number, indicating how many cycles of exercise
# you want.  The more cycles, the more logfiles will be generated.
# The ratio is about two cycles per logfile.
function exercise
{
   limit=${1}

   saved_cwd=`pwd`
   cd ${SANDBOX}/wc

   echo ""
   i=1
   while [ ${i} -le ${limit} ]; do
     mv a1 a2
     mv b1 b2
     mv c1 c2
     ${SVN} -q rm a1 b1 c1
     ${SVN} -q add a2 b2 c2
     ${SVN} -q ci -m "Move 1s to 2s, but not as cheap copies."

     mv a2 a1
     mv b2 b1
     mv c2 c1
     ${SVN} -q rm a2 b2 c2
     ${SVN} -q add a1 b1 c1
     ${SVN} -q ci -m "Move 2s back to 1s, same way."

     echo "Exercising repository, pass ${i} of ${limit}."
     i=`dc -e "${i} 1 + p"`
   done
   echo ""

   cd ${saved_cwd}
}

# Generate some logfile activity.
exercise 10

# Do a full backup.
head=`${SVNLOOK} youngest ${REPOS}`
echo "Starting full backup (at r${head})..."
mkdir ${FULL_BACKUPS}
mkdir ${FULL_BACKUPS}/${PROJ}
mkdir ${FULL_BACKUPS}/${PROJ}/repos
mkdir ${FULL_BACKUPS}/${PROJ}/logs
cd ${REPOS}/db
${DB_ARCHIVE} > ${RECORDS}/${PROJ}-full-backup-inactive-logfiles
cd ../..
cp -a ${REPOS} ${FULL_BACKUPS}/${PROJ}/repos/
cd ${REPOS}/db
for logfile in `${DB_ARCHIVE} -l`; do
  # For maximum paranoia, we want repository activity *while* we're
  # making the full backup.
  exercise 5
  cp ${logfile} ${FULL_BACKUPS}/${PROJ}/logs
done
cat ${RECORDS}/${PROJ}-full-backup-inactive-logfiles | xargs rm -f
cd ../..
echo "Full backup completed (r${head} was head when started)."

# Do the incremental backups for a nominal week.
for day in 1 2 3 4 5 6; do
  exercise 5
  head=`${SVNLOOK} youngest ${REPOS}`
  echo "Starting incremental backup ${day} (at r${head})..."
  mkdir ${INCREMENTAL_PREFIX}-${day}
  mkdir ${INCREMENTAL_PREFIX}-${day}/${PROJ}
  cd ${REPOS}/db
  ${DB_ARCHIVE} > ${RECORDS}/${PROJ}-incr-backup-${day}-inactive-logfiles
  for logfile in `${DB_ARCHIVE} -l`; do
    # For maximum paranoia, we want repository activity *while* we're
    # making the incremental backup.  But if we did commits with each
    # logfile copy, this script would be quite slow (Fibonacci effect). 
    # So we only exercise on the last two "days" of incrementals.
    if [ ${day} -ge 5 ]; then
      exercise 3
    fi
    cp ${logfile} ${INCREMENTAL_PREFIX}-${day}/${PROJ}
  done
  cat ${RECORDS}/${PROJ}-incr-backup-${day}-inactive-logfiles | xargs rm -f
  cd ../..
  echo "Incremental backup ${day} done (r${head} was head when started)."
done

# The last revision a restoration is guaranteed to contain is whatever
# was head at the start of the last incremental backup.
last_guaranteed_rev=${head}

# Make the repository vanish, so we can restore it.
mv ${REPOS} was_${REPOS}

echo ""
echo "Oliver Cromwell has destroyed the repository!  Restoration coming
up..."
echo ""

# Restore.
#
# After copying the full repository backup over, we remove the shared
# memory segments and the dav/* stuff.  Recovery recreates the shmem
# segments, and anything in dav/* is certainly obsolete if we're doing
# a restore.
#
# Note that we use db_recover instead of 'svnadmin recover'.  This is
# because we want to pass the -c ('catastrophic') flag to db_recover.
# As of Subversion 1.0.x, there is no '--catastrophic' flag to
# 'svnadmin recover', unfortunately.
cp -a ${FULL_BACKUPS}/${PROJ}/repos/${REPOS} .
cp -a ${FULL_BACKUPS}/${PROJ}/logs/* ${REPOS}/db
rm -rf ${REPOS}/db/__db*
rm -rf ${REPOS}/dav/*
cd ${REPOS}/db
${DB_RECOVER} -ce
cd ../..
head=`${SVNLOOK} youngest ${REPOS}`
echo ""
echo "(Restored from full backup to r${head}...)"
for day in 1 2 3 4 5 6; do
  cd ${REPOS}/db
  cp ${INCREMENTAL_PREFIX}-${day}/${PROJ}/* .
  ${DB_RECOVER} -ce
  cd ../..
  head=`${SVNLOOK} youngest ${REPOS}`
  echo "(Restored from incremental-${day} to r${head}...)"
done
echo ""
echo "Restoration complete.  All hail the King."

# Verify the restoration.
was_head=`${SVNLOOK} youngest was_${REPOS}`
restored_head=`${SVNLOOK} youngest ${REPOS}`
echo ""
echo "Highest revision in original repository:  ${was_head}"
echo "Highest revision restored:                ${restored_head}"
echo ""
echo "(It's okay if restored is less than original, even much less.)"

if [ ${restored_head} -lt ${last_guaranteed_rev} ]; then
   echo ""
   echo "Restoration failed because r${restored_head} is too low --"
   echo "should have restored to at least r${last_guaranteed_rev}."
   exit 1
fi

# Looks like we restored at least to the minimum required revision.
# Let's do some spot checks, though.

echo ""
echo "Comparing logs up to r${restored_head} for both repositories..."
${SVN} log -v -r1:${restored_head} file://`pwd`/was_${REPOS} > a
${SVN} log -v -r1:${restored_head} file://`pwd`/${REPOS}     > b
if cmp a b; then
  echo "Done comparing logs."
else
  echo "Log comparison failed -- restored repository is not right."
  exit 1
fi

echo ""
echo "Comparing r${restored_head} exported trees from both repositories..."
${SVN} -q export -r${restored_head} file://`pwd`/was_${REPOS} orig-export
${SVN} -q export -r${restored_head} file://`pwd`/${REPOS} restored-export
if diff -q -r orig-export restored-export; then
  echo "Done comparing r${restored_head} exported trees."
else
  echo "Recursive diff failed -- restored repository is not right."
fi

echo ""
echo "Done."
