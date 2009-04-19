#!/bin/sh

libtool_scripts="
subversion/svn/svn
subversion/svnadmin/svnadmin
subversion/svndumpfilter/svndumpfilter
subversion/svnlook/svnlook
subversion/svnserve/svnserve
subversion/svnsync/svnsync
subversion/svnversion/svnversion
subversion/tests/cmdline/entries-dump
$(find subversion/tests -name '*-test' ! -path '*/.libs/*')
"

lib_directories="$(find subversion -name 'libsvn_*.so' -a ! -name 'libsvn_test-1.so' | sed -e "s:^:$(pwd)/:")"
lib_directories="$(echo $lib_directories)"

_sed()
{
  sed -e "$@" "$libtool_script" > "$libtool_script.new"
  mv -f "$libtool_script.new" "$libtool_script"
}

for libtool_script in $libtool_scripts; do
  if test -f "$libtool_script"; then
    if grep LD_LIBRARY_PATH "$libtool_script" > /dev/null && ! grep LD_PRELOAD "$libtool_script" > /dev/null; then
      echo "Transforming $libtool_script"
      _sed "/export LD_LIBRARY_PATH/aLD_PRELOAD=\"$lib_directories\""
      _sed "/LD_PRELOAD=/aexport LD_PRELOAD"
      chmod +x "$libtool_script"
    fi
  fi
done
