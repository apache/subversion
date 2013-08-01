dnl ===================================================================
dnl   Licensed to the Apache Software Foundation (ASF) under one
dnl   or more contributor license agreements.  See the NOTICE file
dnl   distributed with this work for additional information
dnl   regarding copyright ownership.  The ASF licenses this file
dnl   to you under the Apache License, Version 2.0 (the
dnl   "License"); you may not use this file except in compliance
dnl   with the License.  You may obtain a copy of the License at
dnl
dnl     http://www.apache.org/licenses/LICENSE-2.0
dnl
dnl   Unless required by applicable law or agreed to in writing,
dnl   software distributed under the License is distributed on an
dnl   "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
dnl   KIND, either express or implied.  See the License for the
dnl   specific language governing permissions and limitations
dnl   under the License.
dnl ===================================================================
dnl
# Miscellaneous additional macros for Subversion's own use.

# SVN_CONFIG_NICE(FILENAME)
# Write a shell script to FILENAME (typically 'config.nice') which reinvokes
# configure with all of the arguments.  Reserves use of the filename
# FILENAME.old for its own use.
# This is different from 'config.status --recheck' in that it does add implicit
# --no-create --no-recursion options, and stores _just_ the configure
# invocation, instead of the entire configured state.
AC_DEFUN([SVN_CONFIG_NICE], [
  AC_MSG_NOTICE([creating $1])
  # This little dance satisfies Cygwin, which cannot overwrite in-use files.
  if test -f "$1"; then
    mv "$1" "$1.old"
  fi

  cat >"$1" <<EOF
#! /bin/sh
#
# Created by configure

'[$]0' $ac_configure_args "\[$]@"
EOF

  chmod +x "$1"
  rm -f "$1.old"
])


dnl
dnl SVN_CONFIG_SCRIPT(path)
dnl
dnl Make AC_OUTPUT create an executable file.
dnl Accumulate filenames in $SVN_CONFIG_SCRIPT_FILES for AC_SUBSTing to
dnl use in, for example, Makefile distclean rules.
dnl
AC_DEFUN(SVN_CONFIG_SCRIPT, [
  SVN_CONFIG_SCRIPT_FILES="$SVN_CONFIG_SCRIPT_FILES $1"
  AC_CONFIG_FILES([$1], [chmod +x $1])])

dnl Iteratively interpolate the contents of the second argument
dnl until interpolation offers no new result. Then assign the
dnl final result to $1.
dnl
dnl Based on APR_EXPAND_VAR macro
dnl
dnl Example:
dnl
dnl foo=1
dnl bar='${foo}/2'
dnl baz='${bar}/3'
dnl SVN_EXPAND_VAR(fraz, $baz)
dnl   $fraz is now "1/2/3"
dnl 
AC_DEFUN(SVN_EXPAND_VAR,[
svn_last=
svn_cur="$2"
while test "x${svn_cur}" != "x${svn_last}";
do
  svn_last="${svn_cur}"
  svn_cur=`eval "echo ${svn_cur}"`
done
$1="${svn_cur}"
])

dnl SVN_STRIP_FLAG(FLAG_VAR_NAME, FLAG)
dnl
dnl Remove FLAG from the variable FLAG_VAR_NAME, if it exists.  This macro
dnl is primarily used for removing unwanted compiler flags, but is really
dnl just a general wrapper around `sed'.
AC_DEFUN(SVN_STRIP_FLAG,
[
  $1=`echo "$$1" | $SED -e 's/$2//'`
])

dnl SVN_REMOVE_STANDARD_LIB_DIRS(OPTIONS)
dnl
dnl Remove standard library search directories.
dnl OPTIONS is a list of compiler/linker options.
dnl This macro prints input options except -L options whose arguments are
dnl standard library search directories (e.g. /usr/lib).
dnl
dnl This macro is used to avoid linking against Subversion libraries
dnl potentially placed in standard library search directories.
AC_DEFUN([SVN_REMOVE_STANDARD_LIB_DIRS],
[
  input_flags="$1"
  output_flags=""
  filtered_dirs="/lib /lib64 /usr/lib /usr/lib64"
  for flag in $input_flags; do
    filter="no"
    for dir in $filtered_dirs; do
      if test "$flag" = "-L$dir" || test "$flag" = "-L$dir/"; then
        filter="yes"
        break
      fi
    done
    if test "$filter" = "no"; then
      output_flags="$output_flags $flag"
    fi
  done
  if test -n "$output_flags"; then
    printf "%s" "${output_flags# }"
  fi
])

AC_DEFUN([SVN_CHECK_FOR_ATOMIC_BUILTINS],
[
  AC_CACHE_CHECK([whether the compiler provides atomic builtins], [svn_cv_atomic_builtins],
  [AC_TRY_RUN([
  int main()
  {
      unsigned long long val = 1010, tmp, *mem = &val;

      if (__sync_fetch_and_add(&val, 1010) != 1010 || val != 2020)
          return 1;

      tmp = val;

      if (__sync_fetch_and_sub(mem, 1010) != tmp || val != 1010)
          return 1;

      if (__sync_sub_and_fetch(&val, 1010) != 0 || val != 0)
          return 1;

      tmp = 3030;

      if (__sync_val_compare_and_swap(mem, 0, tmp) != 0 || val != tmp)
          return 1;

      if (__sync_lock_test_and_set(&val, 4040) != 3030)
          return 1;

      mem = &tmp;

      if (__sync_val_compare_and_swap(&mem, &tmp, &val) != &tmp)
          return 1;

      __sync_synchronize();

      if (mem != &val)
          return 1;

      return 0;
  }], [svn_cv_atomic_builtins=yes], [svn_cv_atomic_builtins=no], [svn_cv_atomic_builtins=no])])
])
