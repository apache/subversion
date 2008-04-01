dnl
dnl  SVN_LIB_UTF8PROC
dnl
dnl  Check configure options and assign variables related to
dnl  the utf8proc library.
dnl
dnl  If we find the library, set the shell variable
dnl  `svn_lib_utf8proc' to `yes'.  Otherwise, set `svn_lib_utf8proc'
dnl  to `no'.

AC_DEFUN(SVN_LIB_UTF8PROC,
[
  AC_ARG_WITH(utf8proc, [AS_HELP_STRING([--with-utf8proc=PATH],
                                        [Compile with utf8proc in PATH])],
  [
    with_utf8proc="$withval"
    required="yes"
  ],
  [
    with_utf8proc="yes"
    required="no"
  ])

  AC_MSG_CHECKING([whether to look for utf8proc])

  if test "${with_utf8proc}" = "no"; then
    AC_MSG_RESULT([no])
    svn_lib_utf8proc=no
  else
    AC_MSG_RESULT([yes])
    saved_LDFLAGS="$LDFLAGS"
    saved_CPPFLAGS="$CPPFLAGS"
    
    dnl If the user doesn't specify a (valid) directory 
    dnl (or doesn't supply a --with-utf8proc option at all), we
    dnl want to look in the default directories: /usr and /usr/local.
    dnl However, the compiler always looks in /usr/{lib,include} anyway,
    dnl so we only need to look in /usr/local

    if test ! -d ${with_utf8proc}; then
      AC_MSG_NOTICE([Looking in default locations])
      with_utf8proc="/usr/local"
    fi

    SVN_UTF8PROC_INCLUDES="-I${with_utf8proc}/include"
    CPPFLAGS="$CPPFLAGS $SVN_UTF8PROC_INCLUDES"
    LDFLAGS="$LDFLAGS -L${with_utf8proc}/lib"
  
    AC_CHECK_HEADER(utf8proc.h,
      [AC_CHECK_LIB(utf8proc, utf8proc_map,
                     svn_lib_utf8proc=yes,
                     svn_lib_utf8proc=no)],
      svn_lib_utf8proc=no)
  
    AC_MSG_CHECKING([for availability of utf8proc])
    if test "$svn_lib_utf8proc" = "yes"; then
      SVN_UTF8PROC_LIBS="-lutf8proc"
      AC_MSG_RESULT([yes])
    else
      AC_MSG_RESULT([no])

      if test "$required" = "yes"; then
        dnl The user explicitly requested utf8proc, but we couldn't find it.
        dnl Exit with an error message.
        AC_MSG_ERROR([Could not find utf8proc])
      fi
      
      SVN_UTF8PROC_INCLUDES=""
      LDFLAGS="$saved_LDFLAGS"
    fi

    CPPFLAGS="$saved_CPPFLAGS"
  fi
    
  AC_SUBST(SVN_UTF8PROC_INCLUDES)
  AC_SUBST(SVN_UTF8PROC_LIBS)
])
