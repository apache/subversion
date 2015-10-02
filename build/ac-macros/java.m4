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
    dnl Prefer /usr/libexec/java_home, then /Library/Java/Home first
    dnl to try to be nice on Darwin.  We'll correct later if we get
    dnl caught in the tangled web of JAVA_HOME.
    if test -x "$JAVA_HOME/bin/java"; then
      JDK="$JAVA_HOME"
    elif test -x "/usr/libexec/java_home"; then
      JDK=`/usr/libexec/java_home`
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
    JDK_SUITABLE=no
  fi
  if test "$JDK_SUITABLE" = "yes"; then
    AC_MSG_RESULT([$JNI_INCLUDEDIR/jni.h])
  else
    AC_MSG_RESULT([no])
    if test "$where" != "check"; then
      AC_MSG_WARN([no JNI header files found.])
      if test "$os_arch" = "Darwin"; then
        AC_MSG_WARN([You may need to install the latest Java Development package from http://connect.apple.com/.  Apple no longer includes the JNI header files by default on Java updates.])
      fi
    fi
  fi

  if test "$JDK_SUITABLE" = "yes"; then
    JAVA_BIN='$(JDK)/bin'

    JAVA="$JAVA_BIN/java"
    JAVAC="$JAVA_BIN/javac"
    JAVAH="$JAVA_BIN/javah"
    JAVADOC="$JAVA_BIN/javadoc"
    JAR="$JAVA_BIN/jar"

    dnl Once upon a time we preferred Jikes for speed.
    dnl Jikes is dead, long live Jikes!
    AC_ARG_WITH(jikes,
                AS_HELP_STRING([--with-jikes=PATH],
                   [Deprecated. Provided for backward compatibility.]),
    [
      if test "$withval" != "no"; then
        AC_MSG_WARN([The --with-jikes option was ignored])
      fi
    ])

    dnl Add javac flags.
    # The release for "-source" could actually be greater than that
    # of "-target", if we want to cross-compile for lesser JVMs.
    if test -z "$JAVAC_FLAGS"; then
      JAVAC_FLAGS="-target $JAVA_OLDEST_WORKING_VER -source 1.6"
      if test "$enable_debugging" = "yes"; then
        JAVAC_FLAGS="-g -Xlint -Xlint:unchecked -Xlint:serial -Xlint:path $JAVAC_FLAGS"
        if test -z "$JAVAC_COMPAT_FLAGS"; then
          JAVAC_COMPAT_FLAGS="$JAVAC_FLAGS -Xlint:-unchecked -Xlint:-deprecation -Xlint:-dep-ann -Xlint:-rawtypes"
        fi
      fi
    fi

    JNI_INCLUDES="-I$JNI_INCLUDEDIR"
    list="`find "$JNI_INCLUDEDIR" -type d -print`"
    for dir in $list; do
      JNI_INCLUDES="$JNI_INCLUDES -I$dir"
    done
  fi

  dnl We use JDK in the Makefile
  AC_SUBST(JDK)
  AC_SUBST(JAVA)
  AC_SUBST(JAVAC)
  AC_SUBST(JAVAC_FLAGS)
  AC_SUBST(JAVAC_COMPAT_FLAGS)
  AC_SUBST(JAVADOC)
  AC_SUBST(JAVAH)
  AC_SUBST(JAR)
  AC_SUBST(JNI_INCLUDES)
])
