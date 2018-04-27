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
dnl  SVN_LIB_KWALLET
dnl
dnl  Check configure options and assign variables related to KWallet support
dnl

AC_DEFUN(SVN_LIB_KWALLET,
[
  AC_ARG_WITH(kwallet,
    [AS_HELP_STRING([[--with-kwallet[=PATH|INCDIR:LIBDIR]]],
                    [Enable use of KWallet (KDE 5 or 4) for auth credentials.
                     PATH is the KDE install path, alternatively INCDIR:LIBDIR
                     are the header and library install paths. ])],
                    [svn_lib_kwallet="$withval"],
                    [svn_lib_kwallet=no])

  AC_MSG_CHECKING([whether to look for KWallet])
  if test "$svn_lib_kwallet" != "no"; then
    AC_MSG_RESULT([yes])
    case "$host" in
    *-*-darwin*)
      AC_MSG_ERROR([--with-kwallet is not supported on Mac OS X.])
      ;;
    *)
      if test "$svn_enable_shared" = "yes"; then
        if test "$APR_HAS_DSO" = "yes"; then
          if test -n "$PKG_CONFIG"; then
            if test "$HAVE_DBUS" = "yes"; then
              AC_MSG_CHECKING([for Qt])
              if $PKG_CONFIG --exists Qt5Core Qt5DBus Qt5Gui; then
                AC_MSG_RESULT([yes, Qt5])
                qt_pkg_config_names="Qt5Core Qt5DBus Qt5Gui"
                kde_config_name="kf5-config"
                kde_inc_names="KF5/KWallet KF5/KCoreAddons KF5/KI18n"
                kde_lib_names="-lKF5Wallet -lKF5I18n -lKF5CoreAddons -lQt5Gui -lQt5DBus -lQt5Core"
              elif $PKG_CONFIG --exists QtCore QtDBus QtGui; then
                AC_MSG_RESULT([yes, Qt4])
                qt_pkg_config_names="QtCore QtDBus QtGui"
                kde_config_name="kde4-config"
                kde_inc_names="/"
                kde_lib_names="-lkdeui -lkdecore -lQtGui -lQtDBus -lQtCore"
              fi
              if test -n "$qt_pkg_config_names"; then
                if test "$svn_lib_kwallet" != "yes"; then
                  AC_MSG_CHECKING([for $kde_config_name])
                  KDE_CONFIG="$svn_lib_kwallet/bin/$kde_config_name"
                  if test -f "$KDE_CONFIG" && test -x "$KDE_CONFIG"; then
                    AC_MSG_RESULT([yes])
                  else
                    if echo "$svn_lib_kwallet" | $EGREP ":" > /dev/null; then
                      AC_MSG_RESULT([unneeded])
                      KDE_CONFIG="unneeded"
                      kde_incdir=["`echo "$svn_lib_kwallet" | $SED -e "s/:.*//"`"]
                      kde_libdir=["`echo "$svn_lib_kwallet" | $SED -e "s/.*://"`"]
                    else
                      AC_MSG_RESULT([no])
                      KDE_CONFIG=""
                    fi
                  fi
                else
                  AC_PATH_PROG(KDE_CONFIG, $kde_config_name)
                  if test -n "$KDE_CONFIG"; then
                    kde_incdir="`$KDE_CONFIG --install include`"
                    kde_libdir="`$KDE_CONFIG --install lib`"
                  fi
                fi
                if test -n "$KDE_CONFIG"; then
                  if test $kde_config_name = "kf5-config"; then
                    dnl KF5 does not compile with -std=c++98
                    SVN_CXX_MODE_SETUP11
                  fi
                  old_CXXFLAGS="$CXXFLAGS"
                  old_LDFLAGS="$LDFLAGS"
                  old_LIBS="$LIBS"
                  dnl --std=c++11 may be required
                  CXXFLAGS="$CXXFLAGS $CXXMODEFLAGS"
                  AC_MSG_CHECKING([for KWallet])
                  for d in [`$PKG_CONFIG --cflags $qt_pkg_config_names`]; do
                    if test -n ["`echo "$d" | $EGREP -- '^-D[^[:space:]]*'`"]; then
                      CPPFLAGS="$CPPFLAGS $d"
                    fi
                  done
                  qt_include_dirs="`$PKG_CONFIG --cflags-only-I $qt_pkg_config_names`"
                  for kde_inc_name in $kde_inc_names; do
                    kde_kwallet_includes="$kde_kwallet_includes -I$kde_incdir/$kde_inc_name"
                  done
                  SVN_KWALLET_INCLUDES="$DBUS_CPPFLAGS $qt_include_dirs $kde_kwallet_includes"
                  qt_libs_other_options="`$PKG_CONFIG --libs-only-other $qt_pkg_config_names`"
                  SVN_KWALLET_LIBS="$DBUS_LIBS $kde_lib_names $qt_libs_other_options"
                  CXXFLAGS="$CXXFLAGS $SVN_KWALLET_INCLUDES -fPIC"
                  LIBS="$LIBS $SVN_KWALLET_LIBS"
                  qt_lib_dirs="`$PKG_CONFIG --libs-only-L $qt_pkg_config_names`"
                  LDFLAGS="$old_LDFLAGS `SVN_REMOVE_STANDARD_LIB_DIRS($qt_lib_dirs -L$kde_libdir)`"
                  AC_LANG(C++)
                  AC_LINK_IFELSE([AC_LANG_SOURCE([[
#include <kwallet.h>
int main()
{KWallet::Wallet::walletList();}]])], svn_lib_kwallet="yes", svn_lib_kwallet="no")
                  AC_LANG(C)
                  if test "$svn_lib_kwallet" = "yes"; then
                    AC_MSG_RESULT([yes])
                    CXXFLAGS="$old_CXXFLAGS"
                    LIBS="$old_LIBS"
                    if test "$kde_config_name" = "kf5-config"; then
                      AC_DEFINE([SVN_HAVE_KF5], [1], [Defined if KF5 available])
                    fi
                  else
                    AC_MSG_RESULT([no])
                    AC_MSG_ERROR([cannot find KWallet])
                  fi
                else
                  AC_MSG_ERROR([cannot find $kde_config_name])
                fi
              else
                AC_MSG_RESULT([no])
                AC_MSG_ERROR([cannot find Qt])
              fi
            else
              AC_MSG_ERROR([cannot find D-Bus])
            fi
          else
            AC_MSG_ERROR([cannot find pkg-config])
          fi
        else
          AC_MSG_ERROR([APR does not have support for DSOs])
        fi
      else
        AC_MSG_ERROR([--with-kwallet conflicts with --disable-shared])
      fi
    ;;
    esac
  else
    AC_MSG_RESULT([no])
  fi
  AC_SUBST(SVN_KWALLET_INCLUDES)
  AC_SUBST(SVN_KWALLET_LIBS)
])
