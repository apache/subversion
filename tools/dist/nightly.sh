#!/bin/sh
set -e

repo=http://svn.collab.net/repos/svn

while getopts "cd:t:" flag; do
  case $flag in
    d) dir="$OPTARG" ;;
    c) clean="1" ;;
    t) target="$OPTARG" ;;
  esac
done

# Setup directories
if [ -n "$dir" ]; then cd $dir; fi
if [ ! -d "roll" ]; then mkdir roll; fi
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
head=`svn info $repo/branches/1.5.x | grep '^Revision' | cut -d ' ' -f 2`
../roll.sh 1.5.0 $head "-nightly $head"
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
