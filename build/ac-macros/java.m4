dnl
dnl java.m4: Locates the JDK and its include files and libraries.
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

  JDK=none
  JAVA_BIN=none
  JAVAC=none
  JAVAH=none
  JAR=none
  JNI_INCLUDES=none

  JDK_SUITABLE=no
  AC_MSG_CHECKING([for JDK])
  if test $where = check; then
    dnl Prefer /Library/Java/Home first to try to be nice on Darwin.
    dnl We'll correct later if we get caught in the tangled web of JAVA_HOME.
    if test -x "$JAVA_HOME/bin/java"; then
      JDK="$JAVA_HOME"
    elif test -x "/Library/Java/Home/bin/java"; then
      JDK="/Library/Java/Home"
    elif test -x "/usr/bin/java"; then
      JDK="/usr"
    elif test -x "/usr/local/bin/java"; then
      JDK="/usr/local"
    fi
  else
    JDK=$where
  fi

  dnl Correct for Darwin's odd JVM layout.  Ideally, we should use realpath,
  dnl but Darwin doesn't have that utility.  /usr/bin/java is a symlink into
  dnl /System/Library/Frameworks/JavaVM.framework/Versions/CurrentJDK/Commands
  dnl See http://developer.apple.com/qa/qa2001/qa1170.html
  os_arch="`uname`"
  if test "$os_arch" = "Darwin" -a "$JDK" = "/usr" -a -d "/Library/Java/Home"; then
      JDK="/Library/Java/Home"
  fi
  if test -f "$JDK/include/jni.h"; then
    dnl This *must* be fully expanded, or we'll have problems later in find.
    JNI_INCLUDEDIR="$JDK/include"
    JDK_SUITABLE=yes
  else
    AC_MSG_WARN([no JNI header files found.])
    if test "$os_arch" = "Darwin"; then
      AC_MSG_WARN([You may need to install the latest Java Development package from http://connect.apple.com/.  Apple no longer includes the JNI header files by default on Java updates.])
    fi
    JDK_SUITABLE=no
  fi
  AC_MSG_RESULT([$JDK_SUITABLE])

  if test "$JDK_SUITABLE" = "yes"; then
    JAVA_BIN='$(JDK)/bin'

    dnl TODO: Test for Jikes, which should be preferred (for speed) if available
    JAVAC="$JAVA_BIN/javac"
    JAVAH="$JAVA_BIN/javah"
    JAR="$JAVA_BIN/jar"

    JNI_INCLUDES="-I$JNI_INCLUDEDIR"
    list="`find "$JNI_INCLUDEDIR" -type d -print`"
    for dir in $list; do
      JNI_INCLUDES="$JNI_INCLUDES -I$dir"
    done

  fi

  dnl We use JDK in both the swig.m4 macros and the Makefile
  AC_SUBST(JDK)
  AC_SUBST(JAVAC)
  AC_SUBST(JAVAH)
  AC_SUBST(JAR)
  AC_SUBST(JNI_INCLUDES)
])
