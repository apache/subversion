#!/bin/sh
set -e

repo=http://svn.apache.org/repos/asf/subversion
svn=svn

# Parse our arguments
while getopts "cd:t:s:" flag; do
  case $flag in
    d) dir="$OPTARG" ;;
    c) clean="1" ;;
    t) target="$OPTARG" ;;
    s) svn="$OPTARG" ;;
  esac
done

# Setup directories
if [ -n "$dir" ]; then cd $dir; else dir="."; fi
if [ -d "roll" ]; then rm -rf roll; fi
mkdir roll
if [ ! -n "$target" ]; then
  if [ ! -d "target" ]; then mkdir target; fi
  target="target"
fi

abscwd=`cd $dir; pwd`

echo "Will place results in: $target"

# Get the latest versions of the rolling scripts
$svn export $repo/trunk/tools/dist/construct-rolling-environment.sh $dir
$svn export $repo/trunk/tools/dist/roll.sh $dir
$svn export $repo/trunk/tools/dist/dist.sh $dir
$svn export $repo/trunk/tools/dist/gen_nightly_ann.py $dir

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
head=`$svn info $repo/trunk | grep '^Revision' | cut -d ' ' -f 2`
${abscwd}/roll.sh trunk $head "-nightly"
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
