dnl
dnl java.m4: Locates the JDK and its include files and libraries.
dnl
dnl Substitutes the @JDK@ token.
dnl

AC_DEFUN(SVN_CHECK_JDK,
[
  AC_ARG_WITH(jdk,
              AC_HELP_STRING([--with-jdk=PATH],
                             [Try to use 'PATH/include' to find the JNI
                              headers.  If PATH is not specified, look 
                              for a Java Development Kit at JAVA_HOME.]),
  [
    case "$withval" in
      "no")
        JDK_SUITABLE=no
      ;;
      "yes")
        SVN_FIND_JDK(check)
      ;;
      *)
        SVN_FIND_JDK($withval)
      ;;
    esac
  ],
  [
    SVN_FIND_JDK(check)
  ])
])

AC_DEFUN(SVN_FIND_JDK,
[
  where=$1

  AC_MSG_CHECKING([for JDK])
  if test $where = check; then
    if test -d "$JAVA_HOME/include"; then
      JDK="$JAVA_HOME"
      JDK_SUITABLE=yes
    else
      JDK=none
      JDK_SUITABLE=no
    fi
  else
    JDK=$where
    if test -d "$JDK/include"; then
      JDK_SUITABLE=yes
    else
      AC_MSG_WARN([no JNI header files found.])
    fi
  fi
  AC_MSG_RESULT([$JDK_SUITABLE])

  AC_SUBST(JDK)
])
