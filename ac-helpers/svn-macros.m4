dnl -----------------------------------------------------------------------
dnl Additional autoconf macros for Subversion
dnl


dnl this writes a "config.nice" file which reinvokes ./configure with all
dnl of the arguments. this is different from config.status which simply
dnl regenerates the output files. config.nice is useful after you rebuild
dnl ./configure (via autoconf or autogen.sh)
AC_DEFUN(SVN_CONFIG_NICE,[
  echo creating $1
  rm -f $1
  cat >$1<<EOF
#! /bin/sh
#
# Created by configure

EOF

  for arg in [$]0 "[$]@"; do
    echo "\"[$]arg\" \\" >> $1
  done
  echo '"[$]@"' >> $1
  chmod +x $1
])

dnl
dnl SVN_SUBDIR_CONFIG(dir [, sub-package-cmdline-args])
dnl
dnl Note that this code is a direct copy of that which is found in 
dnl the apr project's build/apr_common.m4.
AC_DEFUN(SVN_SUBDIR_CONFIG, [
  if test "$do_subdir_config" = "yes" ; then
    # save our work to this point; this allows the sub-package to use it
    AC_CACHE_SAVE

    echo "configuring package in $1 now"
    ac_popdir=`pwd`
    ac_abs_srcdir=`(cd $srcdir/$1 && pwd)`
    apr_config_subdirs="$1"
    test -d $1 || $MKDIR $1
    cd $1

    changequote(, )dnl
    # A "../" for each directory in /$config_subdirs.
    ac_dots=`echo $apr_config_subdirs|sed -e 's%^\./%%' -e 's%[^/]$%&/%' -e 's%[^/]*/%../%g'`
    changequote([, ])dnl

    # Make the cache file name correct relative to the subdirectory.
    case "$cache_file" in
    /*) ac_sub_cache_file=$cache_file ;;
    *) # Relative path.
      ac_sub_cache_file="$ac_dots$cache_file" ;;
    esac

    # The eval makes quoting arguments work.
    if eval $ac_abs_srcdir/configure $ac_configure_args --cache-file=$ac_sub_cache_file --srcdir=$ac_abs_srcdir $2
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
    AC_MSG_WARN(not running configure in $1)
  fi
])dnl
