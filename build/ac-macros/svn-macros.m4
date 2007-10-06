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


# SVN_EXTERNAL_PROJECT_SETUP()
# Internal helper for SVN_EXTERNAL_PROJECT.
AC_DEFUN([SVN_EXTERNAL_PROJECT_SETUP], [
  do_subdir_config="yes"
  AC_ARG_ENABLE([subdir-config],
    AS_HELP_STRING([--disable-subdir-config],
                   [do not reconfigure packages in subdirectories]),
    [if test "$enableval" = "no"; then do_subdir_config="no"; fi])
  AC_SUBST([SVN_EXTERNAL_PROJECT_SUBDIRS], [""])
])

# SVN_EXTERNAL_PROJECT(SUBDIR [, ADDITIONAL-CONFIGURE-ARGS])
# Setup SUBDIR as an external project. This means:
# - Execute the configure script immediately at the point of macro invocation.
# - Add SUBDIR to the substitution variable SVN_EXTERNAL_PROJECT_SUBDIRS,
#   for the Makefile.in to arrange to execute make in the subdir.
#
# Derived from APR_SUBDIR_CONFIG
AC_DEFUN([SVN_EXTERNAL_PROJECT], [
  AC_REQUIRE([SVN_EXTERNAL_PROJECT_SETUP])
  SVN_EXTERNAL_PROJECT_SUBDIRS="$SVN_EXTERNAL_PROJECT_SUBDIRS $1"
  if test "$do_subdir_config" = "yes" ; then
    # save our work to this point; this allows the sub-package to use it
    AC_CACHE_SAVE

    AC_MSG_NOTICE([configuring package in $1 now])
    ac_popdir=`pwd`
    ac_abs_srcdir=`(cd $srcdir/$1 && pwd)`
    apr_config_subdirs="$1"
    test -d $1 || $MKDIR $1
    cd $1

    # A "../" for each directory in /$config_subdirs.
    ac_dots=[`echo $apr_config_subdirs|sed -e 's%^\./%%' -e 's%[^/]$%&/%' -e 's%[^/]*/%../%g'`]

    # Make the cache file name correct relative to the subdirectory.
    case "$cache_file" in
    /*) ac_sub_cache_file=$cache_file ;;
    *) # Relative path.
      ac_sub_cache_file="$ac_dots$cache_file" ;;
    esac

    # The eval makes quoting arguments work.
    if eval $SHELL $ac_abs_srcdir/configure $ac_configure_args --cache-file=$ac_sub_cache_file --srcdir=$ac_abs_srcdir $2
    then :
      echo "$1 configured properly"
    else
      echo "configure failed for $1"
      exit 1
    fi
    cd $ac_popdir

    # grab any updates from the sub-package
    AC_CACHE_LOAD
  else
    AC_MSG_WARN([not running configure in $1])
  fi
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

dnl SVN_MAYBE_ADD_TO_CFLAGS(option)
dnl
dnl Attempt to compile a trivial C program to test if the option passed
dnl is valid. If it is, then add it to CFLAGS. with the passed in option
dnl and see if it was successfully compiled.
dnl
dnl This macro is usually used for stricter syntax checking flags.
dnl Therefore we include certain headers which may in turn include system
dnl headers, as system headers on some platforms may fail strictness checks
dnl we wish to use on other platforms.

AC_DEFUN(SVN_MAYBE_ADD_TO_CFLAGS,
[
  option="$1"
  svn_maybe_add_to_cflags_saved_flags="$CFLAGS"
  CFLAGS="$CFLAGS $option"
  AC_MSG_CHECKING([if $CC accepts $option])
  AC_COMPILE_IFELSE([AC_LANG_PROGRAM(
    [[#include <apr_portable.h>]],
    [[]])],
    [svn_maybe_add_to_cflags_ok="yes"],
    [svn_maybe_add_to_cflags_ok="no"]
  )
  if test "$svn_maybe_add_to_cflags_ok" = "yes"; then
    AC_MSG_RESULT([yes, will use it])
  else
    AC_MSG_RESULT([no])
    CFLAGS="$svn_maybe_add_to_cflags_saved_flags"
  fi
])
