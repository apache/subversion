#!/bin/sh
#
# mailer-init.sh: create and initialize a repository for the mailer tests
#
# USAGE: ./mailer-init.sh
#

scripts="`dirname $0`"
scripts="`cd $scripts && pwd`"

d=mailer-init.$$
mkdir $d
cd $d
echo "test directory is: $d"

svnadmin create repos
svn co file://`pwd`/repos wc
cd wc

# create a bunch of dirs and files
mkdir dir1 dir2
echo file1 > file1
echo file2 > file2
echo file3 > dir1/file3
echo file4 > dir1/file4
echo file5 > dir2/file5
echo file6 > dir2/file6
svn add *
svn commit -m "initial load"

# make some changes
echo change C1 >> file2
echo change C2 >> dir2/file5
svn commit -m "two file changes"

# copy a file and a dir
svn cp file1 dir2/file7
svn cp dir1 dir3
svn commit -m "two copies"

# copy and modify a file
svn cp file1 dir3/file8
echo change C3 >> dir3/file8
svn commit -m "copied and changed"

# add a file, add a dir, and make a change
echo file9 > file9
svn add file9
svn mkdir dir4
echo change C4 >> dir1/file3
svn commit -m "mixed addition and change"

# add a file, add a dir, delete a file, delete a dir, and make a change
echo file10 > dir1/file10
svn add dir1/file10
svn mkdir dir3/dir5
svn rm file2 dir2
echo change C5 >> dir3/file3
svn up  # make sure our dirs are up to date
svn commit -m "adds, deletes, and a change"

# copy a dir and change a file in it
svn cp dir3 dir6
echo change C6 >> dir6/file4
svn commit -m "copy dir, then make a change"


# tweak the commit dates to known quantities
$scripts/mailer-tweak.py ../repos
