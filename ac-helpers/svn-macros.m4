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

  for arg in [$]0 $ac_configure_args; do
    echo "\"[$]arg\" \\" >> $1
  done
  echo '"[$]@"' >> $1
  chmod +x $1
])
