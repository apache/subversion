#!/bin/sh
#
#
# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing,
# software distributed under the License is distributed on an
# "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
# KIND, either express or implied.  See the License for the
# specific language governing permissions and limitations
# under the License.
#
#
set -e

repo=http://svn.apache.org/repos/asf/subversion
svn=svn

# Parse our arguments
while getopts "cd:t:s:" flag; do
  case $flag in
    d) dir="`cd $OPTARG && pwd`" ;; # abspath
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
for i in construct-rolling-environment.sh roll.sh dist.sh gen_nightly_ann.py
do 
  $svn export $repo/trunk/tools/dist/$i $dir/$i
done

# Create the environment
cd roll
echo '----------------building environment------------------'
if [ ! -d "prefix" ]; then
  ../construct-rolling-environment.sh prefix
fi;

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
