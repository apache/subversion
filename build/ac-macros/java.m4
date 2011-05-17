dnl
dnl java.m4: Locates the JDK and its include files and libraries.
dnl

AC_DEFUN(SVN_CHECK_JDK,
[
  JAVA_OLDEST_WORKING_VER="$1"
  AC_ARG_WITH(jdk,
              AS_HELP_STRING([--with-jdk=PATH],
                             [Try to use 'PATH/include' to find the JNI
                              headers.  If PATH is not specified, look 
                              for a Java Development Kit at JAVA_HOME.]),
  [
    case "$withval" in
      "no")
        JDK_SUITABLE=no
      ;;
      "yes")
        SVN_FIND_JDK(check, $JAVA_OLDEST_WORKING_VER)
      ;;
      *)
        SVN_FIND_JDK($withval, $JAVA_OLDEST_WORKING_VER)
      ;;
    esac
  ],
  [
    SVN_FIND_JDK(check, $JAVA_OLDEST_WORKING_VER)
  ])
])

AC_DEFUN(SVN_FIND_JDK,
[
  where=$1
  JAVA_OLDEST_WORKING_VER="$2"

  JDK=none
  JAVA_BIN=none
  JAVADOC=none
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
  if test "$os_arch" = "Darwin"; then
    OSX_VER=`/usr/bin/sw_vers | grep ProductVersion | cut -f2 | cut -d"." -f1,2`

    if test "$OSX_VER" = "10.4"; then
      dnl For OS X 10.4, the SDK version is 10.4u instead of 10.4.
      OSX_VER="10.4u"
    fi

    OSX_SYS_JAVA_FRAMEWORK="/System/Library/Frameworks/JavaVM.framework"
    OSX_SDK_JAVA_FRAMEWORK="/Developer/SDKs/MacOSX$OSX_VER.sdk/System/Library"
    OSX_SDK_JAVA_FRAMEWORK="$OSX_SDK_JAVA_FRAMEWORK/Frameworks/JavaVM.framework"
  fi

  if test "$os_arch" = "Darwin" && test "$JDK" = "/usr" &&
     test -d "/Library/Java/Home"; then
    JDK="/Library/Java/Home"
  fi

  if test "$os_arch" = "Darwin" && test "$JDK" = "/Library/Java/Home"; then
    JRE_LIB_DIR="$OSX_SYS_JAVA_FRAMEWORK/Classes"
  else
    JRE_LIB_DIR="$JDK/jre/lib"
  fi

  if test -f "$JDK/include/jni.h"; then
    dnl This *must* be fully expanded, or we'll have problems later in find.
    JNI_INCLUDEDIR="$JDK/include"
    JDK_SUITABLE=yes
  elif test "$os_arch" = "Darwin" && test -e "$JDK/Headers/jni.h"; then
    dnl Search the Headers directory in the JDK
    JNI_INCLUDEDIR="$JDK/Headers"
    JDK_SUITABLE=yes
  elif test "$os_arch" = "Darwin" &&
       test -e "$OSX_SYS_JAVA_FRAMEWORK/Headers/jni.h"; then
    dnl Search the System framework's Headers directory
    JNI_INCLUDEDIR="$OSX_SYS_JAVA_FRAMEWORK/Headers"
    JDK_SUITABLE=yes
  elif test "$os_arch" = "Darwin" &&
       test -e "$OSX_SDK_JAVA_FRAMEWORK/Headers/jni.h"; then
    dnl Search the SDK's System framework's Headers directory
    JNI_INCLUDEDIR="$OSX_SDK_JAVA_FRAMEWORK/Headers"
    JDK_SUITABLE=yes
  else
    AC_MSG_WARN([no JNI header files found.])
    if test "$os_arch" = "Darwin"; then
      AC_MSG_WARN([You may need to install the latest Java Development package from http://connect.apple.com/.  Apple no longer includes the JNI header files by default on Java updates.])
    fi
    JDK_SUITABLE=no
  fi
  AC_MSG_RESULT([$JNI_INCLUDEDIR/jni.h])

  if test "$JDK_SUITABLE" = "yes"; then
    JAVA_BIN='$(JDK)/bin'

    JAVA="$JAVA_BIN/java"
    JAVAC="$JAVA_BIN/javac"
    JAVAH="$JAVA_BIN/javah"
    JAVADOC="$JAVA_BIN/javadoc"
    JAR="$JAVA_BIN/jar"

    dnl Prefer Jikes (for speed) if available.
    jikes_options="/usr/local/bin/jikes /usr/bin/jikes"
    AC_ARG_WITH(jikes,
                AS_HELP_STRING([--with-jikes=PATH],
                               [Specify the path to a jikes binary to use
                                it as your Java compiler.  The default is to
                                look for jikes (PATH optional).  This behavior
                                can be switched off by supplying 'no'.]),
    [
        if test "$withval" != "no" && test "$withval" != "yes"; then
          dnl Assume a path was provided.
          jikes_options="$withval $jikes_options"
        fi
        requested_jikes="$withval"  # will be 'yes' if path unspecified
    ])
    if test "$requested_jikes" != "no"; then
      dnl Look for a usable jikes binary.
      for jikes in $jikes_options; do
        if test -z "$jikes_found" && test -x "$jikes"; then
          jikes_found="yes"
          JAVAC="$jikes"
          JAVA_CLASSPATH="$JRE_LIB_DIR"
          for jar in $JRE_LIB_DIR/*.jar; do
            JAVA_CLASSPATH="$JAVA_CLASSPATH:$jar"
          done
        fi
      done
    fi
    if test -n "$requested_jikes" && test "$requested_jikes" != "no"; then
      dnl Jikes was explicitly requested.  Verify that it was provided.
      if test -z "$jikes_found"; then
        AC_MSG_ERROR([Could not find a usable version of Jikes])
      elif test -n "$jikes_found" && test "$requested_jikes" != "yes" &&
           test "$JAVAC" != "$requested_jikes"; then
        AC_MSG_WARN([--with-jikes PATH was invalid, substitute found])
      fi
    fi

    dnl Add javac flags.
    # The release for "-source" could actually be greater than that
    # of "-target", if we want to cross-compile for lesser JVMs.
    JAVAC_FLAGS="-target $JAVA_OLDEST_WORKING_VER -source 1.3"
    if test "$enable_debugging" = "yes"; then
      JAVAC_FLAGS="-g $JAVAC_FLAGS"
    fi

    JNI_INCLUDES="-I$JNI_INCLUDEDIR"
    list="`find "$JNI_INCLUDEDIR" -type d -print`"
    for dir in $list; do
      JNI_INCLUDES="$JNI_INCLUDES -I$dir"
    done
  fi

  dnl We use JDK in both the swig.m4 macros and the Makefile
  AC_SUBST(JDK)
  AC_SUBST(JAVA)
  AC_SUBST(JAVAC)
  AC_SUBST(JAVAC_FLAGS)
  AC_SUBST(JAVADOC)
  AC_SUBST(JAVAH)
  AC_SUBST(JAR)
  AC_SUBST(JNI_INCLUDES)
])
