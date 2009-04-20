#!/bin/sh

if test "$(uname)" = "Darwin"; then
  exit
fi

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

libraries="auth_gnome_keyring auth_kwallet client delta diff fs fs_base fs_fs fs_util ra ra_local ra_neon ra_serf ra_svn repos subr wc"

for library in $libraries; do
  library_dependencies="$(echo -n $(for x in ${!library}; do echo $x ; done | sort -u))"
  eval "$library=\$library_dependencies"
done

# Dependencies of executables
svn="$auth_gnome_keyring $auth_kwallet $client $delta $diff $ra $subr $wc"
svnadmin="$delta $fs $repos $subr"
svndumpfilter="$delta $fs $repos $subr"
svnlook="$delta $diff $fs $repos $subr"
svnserve="$delta $fs $ra_svn $repos $subr"
svnsync="$delta $ra $subr"
entries_dump="$subr $wc"

executables="svn svnadmin svndumpfilter svnlook svnlook svnserve svnsync entries_dump"

for executable in $executables; do
  if [ "$executable" != entries_dump ]; then
    eval "${executable}_path=subversion/svn/$executable"
  else
    eval "${executable}_path=subversion/tests/cmdline/entries-dump"
  fi
  executable_dependencies="$(echo -n $(for x in ${!executable}; do echo $x ; done | sort -u))"
  eval "$executable=\$executable_dependencies"
done

test_paths="$(find subversion/tests -name '*-test' ! -path '*/.libs/*')"
for test in $test_paths; do
  test_path="$test"
  test_library="$(echo $test | sed -e 's:^subversion/tests/libsvn_\([^/]*\)/.*:\1:')"
  test="$(echo $test | sed -e 's:^subversion/tests/libsvn_[^/]*/\(.*\):\1:' -e 's/-/_/g')"
  test_dependencies="${!test_library}"
  eval "$test=\$test_dependencies"
  eval "${test}_path=\$test_path"
  tests="$tests $test"
done

_sed()
{
  sed -e "$@" "$libtool_script_path" > "$libtool_script_path.new"
  mv -f "$libtool_script_path.new" "$libtool_script_path"
}

for libtool_script in $executables $tests; do
  eval "libtool_script_path=\${${libtool_script}_path}"
  if [ -f "$libtool_script_path" ]; then
    if grep LD_LIBRARY_PATH "$libtool_script_path" > /dev/null && ! grep LD_PRELOAD "$libtool_script_path" > /dev/null; then
      echo "Transforming $libtool_script_path"
      libtool_script_dependencies="${!libtool_script}"
      for libtool_script_dependency in $libtool_script_dependencies; do
        libtool_script_library="$(pwd)/subversion/libsvn_$libtool_script_dependency/.libs/libsvn_$libtool_script_dependency-1.so"
        [ -f "$libtool_script_library" ] && libtool_script_libraries="$libtool_script_libraries $libtool_script_library"
      done
      libtool_script_libraries="${libtool_script_libraries# *}"
      _sed "/export LD_LIBRARY_PATH/aLD_PRELOAD=\"$libtool_script_libraries\""
      _sed "/LD_PRELOAD=/aexport LD_PRELOAD"
      chmod +x "$libtool_script_path"
    fi
  fi
done
