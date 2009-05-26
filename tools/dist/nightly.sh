#!/bin/sh
set -e

repo=http://svn.collab.net/repos/svn

# Get the latest versions of the rolling scripts
svn export $repo/trunk/tools/dist/construct-rolling-environment.sh
svn export $repo/trunk/tools/dist/roll.sh
svn export $repo/trunk/tools/dist/dist.sh
svn export $repo/trunk/tools/dist/gen_nightly_ann.py

while getopts "cd:t:" flag; do
  case $flag in
    d) dir="$OPTARG" ;;
    c) clean="1" ;;
    t) target="$OPTARG" ;;
  esac
done

# Setup directories
if [ -n "$dir" ]; then cd $dir; fi
if [ -d "roll" ]; then rm -rf roll; fi
mkdir roll
if [ ! -n "$target" ]; then
  if [ ! -d "target" ]; then mkdir target; fi
  target="target"
fi

echo "Will place results in: $target"

# Create the environment
cd roll
echo '----------------building environment------------------'
if [ ! -d "prefix" ]; then
  ../construct-rolling-environment.sh prefix
fi;
if [ ! -d "unix-dependencies" ]; then
  ../construct-rolling-environment.sh deps
fi

# Roll the tarballs
echo '-------------------rolling tarball--------------------'
head=`svn info $repo/trunk | grep '^Revision' | cut -d ' ' -f 2`
`dirname $0`/roll.sh trunk $head "-nightly r$head"
cd ..

# Create the information page
./gen_nightly_ann.py $head > index.html

# Move the results to the target location
echo '-------------------moving results---------------------'
if [ -f "$target/index.html" ]; then rm "$target/index.html"; fi
mv index.html "$target"
if [ -d "$target/dist" ]; then rm -r "$target/dist"; fi
rm -r roll/deploy/to-tigris
mv roll/deploy "$target/dist"

# Optionally remove our working directory
if [ -n "$clean" ]; then
  echo '--------------------cleaning up-----------------------'
  rm -r roll
fi

echo '------------------------done--------------------------'
