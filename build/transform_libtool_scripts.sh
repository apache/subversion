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

# Dependencies of libraries
# TODO: generate from build.conf
subr="subr"
auth_gnome_keyring="auth_gnome_keyring $subr"
auth_kwallet="auth_kwallet $subr"
delta="delta $subr"
diff="diff $subr"
fs_util="fs_util $subr"
fs_base="fs_base $delta $fs_util $subr"
fs_fs="fs_fs $delta $fs_util $subr"
fs="fs $fs_base $fs_fs $fs_util $subr"
repos="repos $delta $fs $fs_util $subr"
ra_local="ra_local $delta $fs $fs_util $repos $subr"
ra_neon="ra_neon $delta $subr"
ra_serf="ra_serf $delta $subr"
ra_svn="ra_svn $delta $subr"
ra="ra $delta $ra_local $ra_neon $ra_serf $ra_svn $subr"
wc="wc $delta $diff $subr"
client="client $delta $diff $ra $subr $wc"

# Delete duplicates in dependencies of libraries
ls subversion | grep libsvn_ | while read library_dir; do
  library=`basename $library_dir | sed s/libsvn_//`
  library_dependencies="$(echo -n $(for x in $(eval echo "\$$library"); do echo $x; done | sort -u))"
  eval "$library=\$library_dependencies"
done

# Dependencies of executables
svn="$auth_gnome_keyring $auth_kwallet $client $delta $diff $ra $subr $wc"
svnadmin="$delta $fs $repos $subr"
svndumpfilter="$delta $fs $repos $subr"
svnlook="$delta $diff $fs $repos $subr"
svnrdump="$auth_gnome_keyring $auth_kwallet $client $delta $ra $repos $subr"
svnserve="$delta $fs $ra_svn $repos $subr"
svnsync="$auth_gnome_keyring $auth_kwallet $delta $ra $subr"
svnversion="$subr $wc"
entries_dump="$subr $wc"
atomic_ra_revprop_change="$subr $ra"

# Variable 'executables' containing names of variables corresponding to executables
executables="svn svnadmin svndumpfilter svnlook svnrdump svnserve svnsync svnversion atomic_ra_revprop_change entries_dump"

for executable in $executables; do
  # Set variables containing paths of executables
  eval "${executable}_path=subversion/$executable/$executable"
  if [ "$executable" = entries_dump ]; then
    eval "${executable}_path=subversion/tests/cmdline/entries-dump"
  fi
  if [ "$executable" = atomic_ra_revprop_change ]; then
    eval "${executable}_path=subversion/tests/cmdline/atomic-ra-revprop-change"
  fi
  # Delete duplicates in dependencies of executables
  executable_dependencies="$(echo -n $(for x in $(eval echo "\$$executable"); do echo $x; done | sort -u))"
  eval "$executable=\$executable_dependencies"
done

test_paths="$(find subversion/tests -mindepth 2 -maxdepth 2 -name '*-test' ! -path '*/.libs/*' | sort)"
for test in $test_paths; do
  test_path="$test"
  # Dependencies of tests are based on names of directories containing tests
  test_library="$(echo $test | sed -e 's:^subversion/tests/libsvn_\([^/]*\)/.*:\1:')"
  test_dependencies="$(eval echo "\$$test_library")"
  # Set variables corresponding to tests and containing dependencies of tests
  test="$(echo $test | sed -e 's:^subversion/tests/libsvn_[^/]*/\(.*\):\1:' -e 's/-/_/g')"
  eval "$test=\$test_dependencies"
  # Set variables containing paths of tests
  eval "${test}_path=\$test_path"
  # Set variable 'tests' containing names of variables corresponding to tests
  tests="$tests $test"
done

# auth-test dynamically loads libsvn_auth_gnome_keyring and libsvn_auth_kwallet libraries
auth_test="auth_gnome_keyring auth_kwallet $auth_test"

# Usage: sed_append LINE_NUMBER TEXT FILE
sed_append()
{
  sed -e "$1a\\
$2" "$3" > "$3.new"
  mv -f "$3.new" "$3"
}

current_directory="$(pwd)"
for libtool_script in $executables $tests; do
  eval "libtool_script_path=\$${libtool_script}_path"
  libtool_script_libraries=""
  if [ -f "$libtool_script_path" ]; then
    if { grep LD_LIBRARY_PATH "$libtool_script_path" && ! grep LD_PRELOAD "$libtool_script_path"; } > /dev/null; then
      echo "Transforming $libtool_script_path"
      libtool_script_dependencies="$(eval echo "\$$libtool_script")"
      for libtool_script_dependency in $libtool_script_dependencies; do
        libtool_script_library="$current_directory/subversion/libsvn_$libtool_script_dependency/.libs/libsvn_$libtool_script_dependency-1.so"
        [ -f "$libtool_script_library" ] && libtool_script_libraries="$libtool_script_libraries $libtool_script_library"
      done
      libtool_script_libraries="${libtool_script_libraries# *}"
      # Append definitions of LD_PRELOAD to libtool scripts
      sed_append 4 "LD_PRELOAD=\"$libtool_script_libraries\"" "$libtool_script_path"
      sed_append 5 "export LD_PRELOAD" "$libtool_script_path"
      chmod +x "$libtool_script_path"
    fi
  fi
done
