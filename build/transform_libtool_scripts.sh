#!/bin/sh

# Dependencies of libraries
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

# Variable 'libraries' containing names of variables corresponding to libraries
libraries="auth_gnome_keyring auth_kwallet client delta diff fs fs_base fs_fs fs_util ra ra_local ra_neon ra_serf ra_svn repos subr wc"

for library in $libraries; do
  # Delete duplicates in dependencies of libraries
  library_dependencies="$(echo -n $(for x in $(eval echo "\$$library"); do echo $x; done | sort -u))"
  eval "$library=\$library_dependencies"
done

# Dependencies of executables
svn="$auth_gnome_keyring $auth_kwallet $client $delta $diff $ra $subr $wc"
svnadmin="$delta $fs $repos $subr"
svndumpfilter="$delta $fs $repos $subr"
svnlook="$delta $diff $fs $repos $subr"
svnserve="$delta $fs $ra_svn $repos $subr"
svnsync="$auth_gnome_keyring $auth_kwallet $delta $ra $subr"
svnversion="$subr $wc"
entries_dump="$subr $wc"

# Variable 'executables' containing names of variables corresponding to executables
executables="svn svnadmin svndumpfilter svnlook svnserve svnsync svnversion entries_dump"

for executable in $executables; do
  # Set variables containing paths of executables
  if [ "$executable" != entries_dump ]; then
    eval "${executable}_path=subversion/$executable/$executable"
  else
    eval "${executable}_path=subversion/tests/cmdline/entries-dump"
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
  sed -e "$1a\
$2" "$3" > "$3.new"
  mv -f "$3.new" "$3"
}

current_directory="$(pwd)"
for libtool_script in $executables $tests; do
  eval "libtool_script_path=\$${libtool_script}_path"
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
