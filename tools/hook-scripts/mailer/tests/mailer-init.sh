#!/bin/sh
#
# mailer-init.sh: create and initialize a repository for the mailer tests
#
# USAGE: ./mailer-init.sh
#

scripts="`dirname $0`"
scripts="`cd $scripts && pwd`"

d=$scripts/mailer-init.$$
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

# make some changes and set some properties
svn ps prop1 propval1 file1
echo change C1 >> file2
svn ps svn:keywords Id file2
svn ps svn:new_svn_prop val file2
svn ps prop1 propval1 file2
svn ps prop3 propval3 dir1
echo change C2 >> dir2/file5
svn commit -m "two file changes"

# copy a file and a dir and change property
svn cp file1 dir2/file7
svn cp dir1 dir3
svn ps prop3 propval4 dir3
svn commit -m "two copies"

# copy and modify a file
svn cp file1 dir3/file8
echo change C3 >> dir3/file8
svn commit -m "copied and changed"

# change and delete properties
svn ps svn:keywords Date file2
svn ps prop2 propval2 file2
svn pd prop1 file2
svn pd svn:new_svn_prop file2
svn ps prop3 propval4 dir1
svn pd prop3 dir3
svn up  # make sure our dirs are up to date
svn commit -m "changes and deletes of properties"

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

# add a binary file and set property to binary value
echo -e "\x00\x01\x02\x03\x04" > file11
svn add file11
svn ps prop2 -F file11 file9 
svn commit -m "add binary file"

# change the binary file and set property to non binary value
echo -e "\x20\x01\x02\x20" > file11
svn ps prop2 propval2 file9 
svn commit -m "change binary file"

# tweak the commit dates to known quantities
$scripts/mailer-tweak.py ../repos
